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
#include <sys/queue.h>
#include <semaphore.h>
#include <stdlib.h>

#include "ts.h"

struct sf_entry {
	struct mlxdevm_port *port;
	char sf_sys_name[512];
	char sf_sys_path[512];
	TAILQ_ENTRY(sf_entry) entry;
	int sfnum;
};

TAILQ_HEAD(sf_list_head, sf_entry);

struct port_add_params {
	struct mlxdevm *dl_fd;
	struct time_stats port_add_stats;
	struct time_stats port_cap_stats;
	struct time_stats port_activate_stats;
};

struct udev_params {
	struct time_stats dev_udev_bind_stats;
	struct time_stats dev_cfg_params_stats;
	struct sf_list_head sf_list_head;
	pthread_mutex_t sf_list_mutex;
	struct udev_monitor *monitor;
	struct udev *udev;
	int sfs_seen;
};

struct bind_params {
	struct mlxdevm *dl_fd;
	struct sf_list_head sf_fifo_head;
	pthread_mutex_t sf_fifo_mutex;
	sem_t sem;
	struct time_stats dev_drv_bind_stats;
	struct time_stats port_attached_stats;
	int num_sfs;
	int success_count;
};

struct ctx_params {
	const char *dl;
	const char *bus;
	const char *dev;
	struct port_add_params port_add_params;
	struct udev_params udev_params;
	struct bind_params bind_params[16];
	int num_sfs;
	int threads;
	uint16_t pfnum;
};

static struct ctx_params _ctx;

static struct mlxdevm_port *
sf_active_port_create(struct mlxdevm *dl, struct ctx_params *ctx, uint32_t sfnum, int i)
{
	struct port_add_params *port_params = &ctx->port_add_params;
	struct mlxdevm_port_fn_ext_cap cap = {};
	struct ts_time port_add_stat = { 0 };
	struct ts_time port_cap_stat = { 0 };
	struct ts_time port_act_stat = { 0 };
	struct mlxdevm_port *port;
	uint8_t state;
	int err;

	ts_log_start_time(&port_add_stat);
	port = mlxdevm_sf_port_add(dl, ctx->pfnum, sfnum);
	if (!port) {
		fprintf(stderr, "%s sf port add fail %d\n", __func__, errno);
		return NULL;
	}
	ts_log_end_time(&port_add_stat);
	ts_update_time_stats(&port_add_stat, &port_params->port_add_stats);

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
	ts_update_time_stats(&port_cap_stat, &port_params->port_cap_stats);

	state = MLXDEVM_PORT_FN_STATE_ACTIVE;
	ts_log_start_time(&port_act_stat);
	err = mlxdevm_port_fn_state_set(dl, port, state);
	if (err) {
		fprintf(stderr, "%s fail to activate sf %d\n", __func__, err);
		return NULL;
	}
	ts_log_end_time(&port_act_stat);
	ts_update_time_stats(&port_act_stat, &port_params->port_activate_stats);
	return port;
}

static int _udev_init(struct udev_params *params)
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

static void _udev_destroy(struct udev_params *params)
{
	udev_monitor_unref(params->monitor);
	udev_unref(params->udev);
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

static int sf_cfg_params_set(struct ctx_params *ctx, struct sf_entry *entry)
{
	struct ts_time ts = { 0 };
	struct mlxdevm *dl;

	ts_log_start_time(&ts);

	dl = mlxdevm_open(ctx->dl, "auxiliary", entry->sf_sys_name);
	if (!dl) {
		fprintf(stderr, "%s fail to connect to %s %d\n", __func__,
			entry->sf_sys_name, errno);
		goto out;
	}

	set_params(dl);
	mlxdevm_close(dl);
out:
	ts_log_end_time(&ts);
	ts_update_time_stats(&ts, &ctx->udev_params.dev_cfg_params_stats);
	return 0;
}

static void queue_to_bind_thread(struct ctx_params *ctx, struct sf_entry *entry)
{
	struct bind_params *params;
	int thread_idx;

	thread_idx = entry->sfnum % ctx->threads;
	params = &ctx->bind_params[thread_idx];
	pthread_mutex_lock(&params->sf_fifo_mutex);
	TAILQ_INSERT_TAIL(&params->sf_fifo_head, entry, entry);
	pthread_mutex_unlock(&params->sf_fifo_mutex);
	sem_post(&params->sem);
}

static int sf_udev_process(struct ctx_params *ctx, struct udev_params *params)
{
#define SF_UDEV_TIME_WAIT_US	(1000u)
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
	struct sf_entry *entry;
	struct sf_entry *found;

	ts_log_start_time(&ts);

	udev = params->udev;
	monitor = params->monitor;

	udev_ref(udev);
	udev_monitor_ref(monitor);

	do {
		if (params->sfs_seen == ctx->num_sfs)
			break;

		//usleep(SF_UDEV_TIME_WAIT_US);
		udev_dev = udev_monitor_receive_device(monitor);
		if (!udev_dev)
			continue;

		action = udev_device_get_action(udev_dev);
		if (strcmp(action, "bind")) {
			udev_device_unref(udev_dev);
			continue;
		}

		sf_num = udev_device_get_sysattr_value(udev_dev, "sfnum");
		if (!sf_num) {
			udev_device_unref(udev_dev);
			continue;
		}
		sf_sys_path = udev_device_get_syspath(udev_dev);
		parent_dev = udev_device_get_parent(udev_dev);
		pci_name = udev_device_get_sysname(parent_dev);
		if (strcmp(pci_name, ctx->dev)) {
			udev_device_unref(udev_dev);
			continue;
		}
		found = NULL;
		pthread_mutex_lock(&params->sf_list_mutex);
		TAILQ_FOREACH(entry, &params->sf_list_head, entry) {
			snprintf(sf_num_in, sizeof(sf_num_in), "%d", entry->sfnum);
			if (!strcmp(sf_num_in, sf_num)) {
				ret = 0;
				found = entry;
				TAILQ_REMOVE(&params->sf_list_head, entry, entry);
				break;
			}
		}
		pthread_mutex_unlock(&params->sf_list_mutex);

		if (found) {
			sf_sys_name = udev_device_get_sysname(udev_dev);
			//printf("Path: %s, sfname: %s %d\n", sf_sys_path, sf_sys_name, params->sfs_seen);
			strcpy(entry->sf_sys_name, sf_sys_name);
			strcpy(entry->sf_sys_path, sf_sys_path);

			err = sf_cfg_params_set(ctx, entry);
			if (err) {
				printf("fail to configure params %d\n", err);
				break;
			}
			params->sfs_seen++;
			queue_to_bind_thread(ctx, found);
		} else {
			//sf_sys_name = udev_device_get_sysname(udev_dev);
			//printf("SF not found %s %s\n", sf_sys_path, sf_sys_name);
		}
		udev_device_unref(udev_dev);
	} while (1);

	udev_monitor_unref(monitor);
	udev_unref(udev);
	ts_log_end_time(&ts);
	ts_update_time_stats(&ts, &params->dev_udev_bind_stats);
	return ret;
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

static int sf_bind(struct bind_params *params, struct sf_entry *entry)
{
	struct ts_time ts = { 0 };

	ts_log_start_time(&ts);
	sf_unbind_cfg_drv(entry->sf_sys_name);
	sf_bind_drv(entry->sf_sys_name);
	ts_log_end_time(&ts);
	ts_update_time_stats(&ts, &params->dev_drv_bind_stats);
	return 0;
}

static int port_attach_wait(struct bind_params *params, struct sf_entry *entnry)
{
	#if 0
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
#endif
	return 0;
}

static void *port_add_worker(void *arg)
{
	struct ctx_params *ctx = arg;
	struct port_add_params *add_params = &ctx->port_add_params;
	struct udev_params *udev_params = &ctx->udev_params;
	struct mlxdevm_port *port;
	struct sf_entry *entry;
	struct mlxdevm *dl;
	uint32_t sfnum;
	int i;

	dl = mlxdevm_open(ctx->dl, ctx->bus, ctx->dev);
	if (!dl) {
		fprintf(stderr, "%s fail to connect to mlxdevm %d\n", __func__, errno);
		return NULL;
	}

	for (i = 0; i < ctx->num_sfs; i++) {
		sfnum = i + 1;
		port = sf_active_port_create(dl, ctx, sfnum, i);
		if (!port)
			continue;

		entry = calloc(1, sizeof(*entry));
		entry->port = port;
		entry->sfnum = sfnum;

		pthread_mutex_lock(&ctx->udev_params.sf_list_mutex);
		TAILQ_INSERT_TAIL(&ctx->udev_params.sf_list_head, entry, entry);
		pthread_mutex_unlock(&ctx->udev_params.sf_list_mutex);
	}
	return NULL;
}

static void *udev_worker(void *arg)
{
	struct ctx_params *ctx = arg;
	struct udev_params *params = &ctx->udev_params;
	int err;

	err = _udev_init(params);
	if (err)
		return NULL;

	sf_udev_process(ctx, params);
	_udev_destroy(params);
	return NULL;
}

int count_entries(struct bind_params *params)
{
	struct sf_entry *entry;
	int count = 0;

	TAILQ_FOREACH(entry, &params->sf_fifo_head, entry)
		count++;
	return count;
}

void fill_array(struct sf_entry **array, struct bind_params *params)
{
	struct sf_entry *entry;
	int i = 0;

	TAILQ_FOREACH(entry, &params->sf_fifo_head, entry) {
		array[i] = entry;
		i++;
	}
}

void del_list(struct bind_params *params)
{
	struct sf_entry *entry;

	while (!TAILQ_EMPTY(&params->sf_fifo_head)) {
		entry = TAILQ_FIRST(&params->sf_fifo_head);
		TAILQ_REMOVE(&params->sf_fifo_head, entry, entry);
	}
}

static void *bind_worker(void *arg)
{
	struct bind_params *params = arg;
	struct sf_entry **array;
	struct sf_entry *entry;
	int count;
	int err;
	int i;

	while (1) {
		if (params->success_count == params->num_sfs)
			break;

		sem_wait(&params->sem);
		pthread_mutex_lock(&params->sf_fifo_mutex);
		count = count_entries(params);
		if (count) {
			array = calloc(count, sizeof(struct sf_entry*));
			fill_array(array, params);
			del_list(params);
		}
		pthread_mutex_unlock(&params->sf_fifo_mutex);

		for (i = 0; i < count; i++) {
			entry = array[i];
			err = sf_bind(params, entry);
			if (err)
				continue;

			err = port_attach_wait(params, entry);
			if (err)
				continue;
			params->success_count++;
		}
	}
	printf("%s exiting\n", __func__);
	return NULL;
}

int main(int argc, char **argv)
{
	struct ts_time ts = { 0 };
	struct time_stats total_stats;
	struct bind_params *params;
	int success_count = 0;
	int expected_count;
	int sfs_per_thread;
	int thread_count;
	pthread_t port_add_tid;
	pthread_t udev_tid;
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

	if (thread_count > 16)
		return EINVAL;

	tids = calloc(thread_count, sizeof(*tids));
	if (!tids)
		return ENOMEM;

	_ctx.dl = argv[1];
	_ctx.bus = argv[2];
	_ctx.dev = argv[3];
	_ctx.num_sfs = sfs_per_thread * thread_count;
	_ctx.threads = thread_count;
	ts_init(&_ctx.port_add_params.port_add_stats);
	ts_init(&_ctx.port_add_params.port_cap_stats);
	ts_init(&_ctx.port_add_params.port_activate_stats);

	TAILQ_INIT(&_ctx.udev_params.sf_list_head);
	pthread_mutex_init(&_ctx.udev_params.sf_list_mutex, NULL);
	ts_init(&_ctx.udev_params.dev_udev_bind_stats);
	ts_init(&_ctx.udev_params.dev_cfg_params_stats);

	for (i = 0; i < thread_count; i++) {
		_ctx.bind_params[i].num_sfs = sfs_per_thread;
		_ctx.bind_params[i].success_count = 0;

		TAILQ_INIT(&_ctx.bind_params[i].sf_fifo_head);
		pthread_mutex_init(&_ctx.bind_params[i].sf_fifo_mutex, NULL);
		sem_init(&_ctx.bind_params[i].sem, 0, 0);
		ts_init(&_ctx.bind_params[i].port_attached_stats);
		ts_init(&_ctx.bind_params[i].dev_drv_bind_stats);
	}

	ts_init(&total_stats);
	ts_log_start_time(&ts);

	/* Make workers ready to process SF binding */
	for (i = 0; i < thread_count; i++) {
		err = pthread_create(&tids[i], NULL, bind_worker, &_ctx.bind_params[i]);
		if (err) {
			printf("%s err = %d\n", err);
			return err;
		}
	}
	err = pthread_create(&udev_tid, NULL, udev_worker, &_ctx);
	if (err)
		return err;

	err = pthread_create(&port_add_tid, NULL, port_add_worker, &_ctx);
	if (err)
		return err;

	pthread_join(udev_tid, NULL);
	pthread_join(port_add_tid, NULL);

	/* port creation and udev event is completed */
	for (i = 0; i < thread_count; i++) {
		pthread_join(tids[i], NULL);
	}
	ts_log_end_time(&ts);
	ts_update_time_stats(&ts, &total_stats);
	ts_print_lat_stats(&total_stats, "total time");

	ts_print_lat_stats(&_ctx.port_add_params.port_add_stats, "port add");
	ts_print_lat_stats(&_ctx.port_add_params.port_cap_stats, "port cap set");
	ts_print_lat_stats(&_ctx.port_add_params.port_activate_stats, "port activate");
	ts_print_lat_stats(&_ctx.udev_params.dev_udev_bind_stats, "udev bind");
	ts_print_lat_stats(&_ctx.udev_params.dev_cfg_params_stats, "cfg param");

	for (i = 0; i < thread_count; i++) {
		ts_print_lat_stats(&_ctx.bind_params[i].dev_drv_bind_stats, "drv bind");
		ts_print_lat_stats(&_ctx.bind_params[i].port_attached_stats , "port_attached");
	}

	for (i = 0; i < thread_count; i++)
		success_count += _ctx.bind_params[i].success_count;

	expected_count = thread_count * sfs_per_thread;
	if (expected_count != success_count) {
		printf("fail to created requested sfs. Created = %d, expeced = %d\n",
			success_count, expected_count);
		return EINVAL;
	}
	return 0;
}
