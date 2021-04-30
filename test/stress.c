/*
 * Copyright © 2021 NVIDIA CORPORATION & AFFILIATES. ALL RIGHTS RESERVED.
 *
 * This software product is a proprietary product of Nvidia Corporation and its
 * affiliates (the "Company") and all right, title, and interest in and to the
 * software product, including all associated intellectual property rights, are
 * and shall remain exclusively with the Company.
 *
 * This software product is governed by the End User License Agreement
 * provided with the software product.
 */

#include <mlxdevm_netlink.h>
#include <mlxdevm.h>
#include <pthread.h>
#include <dirent.h>
#include <stdio.h>
#include <libudev.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdlib.h>

#include "ts.h"

struct sf_dev {
	struct mlxdevm_port *port;
	char sf_sys_name[512];
	char sf_sys_path[512];
};

struct thread_params {
	struct mlxdevm *dl_fd;
	const char *dl;
	const char *bus;
	const char *dev;
	struct udev_monitor *monitor;
	struct udev *udev;
	int num_sfs;
	uint32_t start_sfnum;
	uint16_t pfnum;
	int success_count;
	struct time_stats port_add_stats;
	struct time_stats port_cap_stats;
	struct time_stats port_activate_stats;
	struct time_stats dev_udev_bind_stats;
	struct time_stats dev_cfg_params_stats;
	struct time_stats dev_drv_bind_stats;
	struct time_stats port_attached_stats;
	struct sf_dev *sfs;
};

static struct mlxdevm_port *
sf_active_port_create(struct mlxdevm *dl, struct thread_params *params, uint32_t sfnum, int i)
{
	struct mlxdevm_port_fn_ext_cap cap = {};
	struct ts_time port_add_stat = { 0 };
	struct ts_time port_cap_stat = { 0 };
	struct ts_time port_act_stat = { 0 };
	struct mlxdevm_port *port;
	uint8_t state;
	int err;

	ts_log_start_time(&port_add_stat);
	port = mlxdevm_sf_port_add(dl, params->pfnum, sfnum);
	if (!port) {
		fprintf(stderr, "%s sf port add fail %d\n", __func__, errno);
		return NULL;
	}
	ts_log_end_time(&port_add_stat);
	ts_update_time_stats(&port_add_stat, &params->port_add_stats);

	ts_log_start_time(&port_cap_stat);
	if (port->ext_cap.roce_valid || port->ext_cap.max_uc_macs_valid) {
		cap.roce = false;
		cap.roce_valid = port->ext_cap.roce_valid;

		cap.max_uc_macs = 1;
		cap.max_uc_macs_valid = port->ext_cap.max_uc_macs_valid;

		err = mlxdevm_port_fn_cap_set(dl, port, &cap);
		if (err) {
			printf("err = %d\n", err);
			return NULL;
		}
	}
	ts_log_end_time(&port_cap_stat);
	ts_update_time_stats(&port_cap_stat, &params->port_cap_stats);

	state = MLXDEVM_PORT_FN_STATE_ACTIVE;
	ts_log_start_time(&port_act_stat);
	err = mlxdevm_port_fn_state_set(dl, port, state);
	if (err) {
		fprintf(stderr, "%s fail to activate sf %d\n", __func__, err);
		return NULL;
	}
	ts_log_end_time(&port_act_stat);
	ts_update_time_stats(&port_act_stat, &params->port_activate_stats);
	params->sfs[i].port = port;
	return port;
}

static int _udev_init(struct thread_params *params)
{
	struct udev_monitor *monitor;
	struct udev *udev;
	int ret = ENODEV;

	udev = udev_new();
	if (!udev) {
		printf("Can't create udev");
		return ENODEV;
	}

	monitor = udev_monitor_new_from_netlink(udev, "udev");
	if (!monitor) {
		printf("Can't create udev monitor");
		goto err_monitor;
	}

	ret = udev_monitor_filter_add_match_subsystem_devtype(monitor,
							      "auxiliary",
							      NULL);
	if (ret) {
		printf("Can't create udev monitor filter");
		goto err_filter;
	}

	ret = udev_monitor_enable_receiving(monitor);
	if (ret) {
		printf("Can't enable udev monitor receiving");
		goto err_filter;
	}

	params->monitor = monitor;
	params->udev = udev;
	return 0;

err_filter:
	udev_monitor_unref(monitor);
err_monitor:
	udev_unref(udev);
	return ret;
}

static void _udev_destroy(struct thread_params *params)
{
	udev_monitor_unref(params->monitor);
	udev_unref(params->udev);
}

static int sf_udev_fetch(struct thread_params *params, uint32_t sfnum, int i)
{
#define SF_UDEV_TIME_WAIT_US	(5000u)
#define SF_UDEV_MAX_RETRY	(2000u)
	struct udev_device *parent_dev;
	struct udev_device *udev_dev;
	struct udev_monitor *monitor;
	struct ts_time ts = { 0 };
	char sf_num_in[10] = {};
	const char *sf_sys_path;
	const char *sf_sys_name;
	const char *pci_name;
	const char *sf_num;
	const char *action;
	struct udev *udev;
	int retry = 0;
	int ret = -1;
	int err;

	ts_log_start_time(&ts);

	udev = params->udev;
	monitor = params->monitor;

	udev_ref(udev);
	udev_monitor_ref(monitor);

	snprintf(sf_num_in, sizeof(sf_num_in), "%d", sfnum);
	do {
		if (retry++ > SF_UDEV_MAX_RETRY)
			break;

		usleep(SF_UDEV_TIME_WAIT_US);

		udev_dev = udev_monitor_receive_device(monitor);
		if (!udev_dev)
			continue;

		action = udev_device_get_action(udev_dev);

		if (strcmp(action, "bind")) {
			udev_device_unref(udev_dev);
			continue;
		}

		sf_num = udev_device_get_sysattr_value(udev_dev, "sfnum");
		sf_sys_path = udev_device_get_syspath(udev_dev);
		parent_dev = udev_device_get_parent(udev_dev);
		pci_name = udev_device_get_sysname(parent_dev);
		if (!strcmp(pci_name, params->dev) &&
		    sf_num && !strcmp(sf_num_in, sf_num)) {
			ret = 0;
			sf_sys_name = udev_device_get_sysname(udev_dev);
			//printf("Path: %s, sfname: %s\n", sf_sys_path, sf_sys_name);
			strcpy(params->sfs[i].sf_sys_name, sf_sys_name);
			strcpy(params->sfs[i].sf_sys_path, sf_sys_path);
			udev_device_unref(udev_dev);
			break;
		} else {
			udev_device_unref(udev_dev);
			continue;
		}
	} while (1);

	udev_monitor_unref(monitor);
	udev_unref(udev);
	ts_log_end_time(&ts);
	ts_update_time_stats(&ts, &params->dev_udev_bind_stats);
	return ret;
}

static void set_params(struct mlxdevm *dl)
{
	struct mlxdevm_param param = {};
	int err;

	err = mlxdevm_dev_driver_param_get(dl, "cmpl_eq_depth", &param);
	if (!err) {
		param.u.val_u32 = 64;
		mlxdevm_dev_driver_param_set(dl, "cmpl_eq_depth", &param);
	}
	err = mlxdevm_dev_driver_param_get(dl, "async_eq_depth", &param);
	if (!err) {
		param.u.val_u32 = 64;
		mlxdevm_dev_driver_param_set(dl, "async_eq_depth", &param);
	}
	err = mlxdevm_dev_driver_param_get(dl, "disable_fc", &param);
	if (!err) {
		param.u.val_bool = false;
		mlxdevm_dev_driver_param_set(dl, "disable_fc", &param);
	}
	err = mlxdevm_dev_driver_param_get(dl, "disable_netdev", &param);
	if (!err) {
		param.u.val_bool = true;
		mlxdevm_dev_driver_param_set(dl, "disable_netdev", &param);
	}
	err = mlxdevm_dev_driver_param_get(dl, "max_cmpl_eqs", &param);
	if (!err) {
		param.u.val_u16 = 1;
		mlxdevm_dev_driver_param_set(dl, "max_cmpl_eqs", &param);
	}
}

static int sf_cfg_params_set(struct thread_params *params, uint32_t sfnum, int i)
{
	struct ts_time ts = { 0 };
	struct mlxdevm *dl;

	ts_log_start_time(&ts);

	dl = mlxdevm_open(params->dl, "auxiliary", params->sfs[i].sf_sys_name);
	if (!dl) {
		fprintf(stderr, "%s fail to connect to %s %d\n", __func__,
			params->sfs[i].sf_sys_name, errno);
		goto out;
	}

	set_params(dl);
	mlxdevm_close(dl);
out:
	ts_log_end_time(&ts);
	ts_update_time_stats(&ts, &params->dev_cfg_params_stats);
	return 0;
}

static bool sf_sysfs_validate(const char *sysfs)
{
	bool ret;
	int fd;

	fd = open(sysfs, O_WRONLY);
	ret = fd < 0 ? false : true;
	close(fd);

	return ret;
}

static int
sf_sysfs_write(const char *sysfs, const char *string_to_write)
{
	int fd, ret;

	fd = open(sysfs, O_WRONLY);
	if (fd < 0) {
		printf("sysfs[%s] doesn't exist", sysfs);
		return -1;
	}
	ret = write(fd, string_to_write, strlen(string_to_write));
	ret = ret > 0 ? 0 : -1;
	if (ret)
		printf("failed to write %s to %s", string_to_write, sysfs);
	close(fd);
	return ret;
}

static int sf_unbind_cfg_drv(const char *name)
{
	char sysfs[PATH_MAX] =
		"/sys/bus/auxiliary/drivers/mlx5_core.sf_cfg/unbind";

	if (!sf_sysfs_validate(sysfs))
		return 0;

	return sf_sysfs_write(sysfs, name);
}

static int sf_bind_drv(const char *name)
{
	char sysfs[PATH_MAX] = "/sys/bus/auxiliary/drivers/mlx5_core.sf/bind";

	if (!sf_sysfs_validate(sysfs))
		return 0;

	return sf_sysfs_write(sysfs, name);
}

static int sf_bind(struct thread_params *params, uint32_t sfnum, int i)
{
	struct ts_time ts = { 0 };

	ts_log_start_time(&ts);
	sf_unbind_cfg_drv(params->sfs[i].sf_sys_name);
	sf_bind_drv(params->sfs[i].sf_sys_name);
	ts_log_end_time(&ts);
	ts_update_time_stats(&ts, &params->dev_drv_bind_stats);
	return 0;
}

static int port_attach_wait(struct thread_params *params, uint32_t sfnum, int i)
{
	struct ts_time ts = { 0 };
	int ret;

	ts_log_start_time(&ts);
	ret = mlxdevm_port_fn_opstate_wait_attached(params->dl_fd, params->sfs[i].port);
	if (ret)
		printf("%s failed sfnum = %d sfdev = %s\n", __func__, sfnum,
			params->sfs[i].sf_sys_name);

	ts_log_end_time(&ts);
	ts_update_time_stats(&ts, &params->port_attached_stats);
	return ret;
}

static void *worker(void *arg)
{
	struct thread_params *params = arg;
	struct mlxdevm_port *port;
	struct mlxdevm *dl;
	uint32_t sfnum;
	int err;
	int i;

	err = _udev_init(params);
	if (err)
		return NULL;

	dl = mlxdevm_open(params->dl, params->bus, params->dev);
	if (!dl) {
		fprintf(stderr, "%s fail to connect to mlxdevm %d\n", __func__, errno);
		goto out;
	}
	params->dl_fd = dl;

	for (i = 0; i < params->num_sfs; i++) {
		sfnum = params->start_sfnum + i;

		port = sf_active_port_create(dl, params, sfnum, i);
		if (!port)
			continue;

		err = sf_udev_fetch(params, sfnum, i);
		if (err)
			continue;

		err = sf_cfg_params_set(params, sfnum, i);
		if (err)
			continue;

		err = sf_bind(params, sfnum, i);
		if (err)
			continue;

		err = port_attach_wait(params, sfnum, i);
		if (err)
			continue;
		params->success_count++;
	}
out:
	_udev_destroy(params);
	mlxdevm_close(dl);
	return NULL;
}

int main(int argc, char **argv)
{
	struct ts_time ts = { 0 };
	struct time_stats total_stats;
	struct thread_params *params;
	int success_count = 0;
	int expected_count;
	int sfs_per_thread;
	int thread_count;
	pthread_t *tids;
	int err;
	int i;

	if (argc < 6) {
		printf("format is %s <bus>, <dev> <thread_count> <per_thread_sfs>\n", argv[0]);
		printf("example %s mlxdevm pci 0000:03:00.0 1 12\n", argv[0]);
		printf("example %s devlink pci 0000:03:00.0\n", argv[0]);
		return EINVAL;
	}

	thread_count = atol(argv[4]);
	sfs_per_thread = atol(argv[5]);

	params = calloc(thread_count, sizeof(*params));
	if (!params)
		return ENOMEM;
	tids = calloc(thread_count, sizeof(*tids));
	if (!tids)
		return ENOMEM;

	for (i = 0; i < thread_count; i++) {
		params[i].dl = argv[1];
		params[i].bus = argv[2];
		params[i].dev = argv[3];
		params[i].pfnum = 0;
		params[i].num_sfs = sfs_per_thread;
		params[i].start_sfnum = (i * sfs_per_thread) + 1;
		params[i].success_count = 0;
		params[i].sfs = calloc(sfs_per_thread, sizeof(struct sf_dev));
		ts_init(&params[i].port_add_stats);
		ts_init(&params[i].port_cap_stats);
		ts_init(&params[i].port_activate_stats);
		ts_init(&params[i].dev_udev_bind_stats);
		ts_init(&params[i].dev_cfg_params_stats);
		ts_init(&params[i].dev_drv_bind_stats);
		ts_init(&params[i].port_attached_stats);
	}

	ts_init(&total_stats);
	ts_log_start_time(&ts);
	for (i = 0; i < thread_count; i++) {
		err = pthread_create(&tids[i], NULL, worker, &params[i]);
		if (err) {
			printf("%s err = %d\n", err);
			return err;
		}
	}
	for (i = 0; i < thread_count; i++) {
		pthread_join(tids[i], NULL);
	}
	ts_log_end_time(&ts);
	ts_update_time_stats(&ts, &total_stats);
	ts_print_lat_stats(&total_stats, "total time");

	for (i = 0; i < thread_count; i++) {
		printf("thread = %d\n", i);
		ts_print_lat_stats(&params[i].port_add_stats, "port add");
		ts_print_lat_stats(&params[i].port_cap_stats, "port cap set");
		ts_print_lat_stats(&params[i].port_activate_stats, "port activate");
		ts_print_lat_stats(&params[i].dev_udev_bind_stats, "udev bind");
		ts_print_lat_stats(&params[i].dev_cfg_params_stats, "cfg param");
		ts_print_lat_stats(&params[i].dev_drv_bind_stats, "drv bind");
		ts_print_lat_stats(&params[i].port_attached_stats , "port_attached");
	}

	for (i = 0; i < thread_count; i++)
		success_count += params[i].success_count;

	expected_count = thread_count * sfs_per_thread;
	if (expected_count != success_count) {
		printf("fail to created requested sfs. Created = %d, expeced = %d\n",
			success_count, expected_count);
		return EINVAL;
	}
	return 0;
}
