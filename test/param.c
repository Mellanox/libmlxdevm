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

static void set_params(struct mlxdevm *dl)
{
	struct mlxdevm_param param = {};
	int err;

	err = mlxdevm_dev_driver_param_get(dl, "cmpl_eq_depth", &param);
	if (!err) {
		printf("type = %d\n", param.nla_type);
		printf("val = %d\n", param.u.val_u32);
		param.u.val_u32 = 64;
		mlxdevm_dev_driver_param_set(dl, "cmpl_eq_depth", &param);
	}
	err = mlxdevm_dev_driver_param_get(dl, "async_eq_depth", &param);
	if (!err) {
		printf("type = %d\n", param.nla_type);
		printf("val = %d\n", param.u.val_u32);
		param.u.val_u32 = 64;
		mlxdevm_dev_driver_param_set(dl, "async_eq_depth", &param);
	}
	err = mlxdevm_dev_driver_param_get(dl, "disable_fc", &param);
	if (!err) {
		printf("type = %d\n", param.nla_type);
		printf("val = %d\n", param.u.val_bool);
		param.u.val_bool = false;
		mlxdevm_dev_driver_param_set(dl, "disable_fc", &param);
	}
	err = mlxdevm_dev_driver_param_get(dl, "disable_netdev", &param);
	if (!err) {
		printf("type = %d\n", param.nla_type);
		printf("val = %d\n", param.u.val_bool);
		param.u.val_bool = true;
		mlxdevm_dev_driver_param_set(dl, "disable_netdev", &param);
	}
	err = mlxdevm_dev_driver_param_get(dl, "max_cmpl_eqs", &param);
	if (!err) {
		printf("type = %d\n", param.nla_type);
		printf("val = %d\n", param.u.val_u16);
		param.u.val_u16 = 1;
		mlxdevm_dev_driver_param_set(dl, "max_cmpl_eqs", &param);
	}
}

int main(int argc, char **argv)
{
	struct mlxdevm *dl;

	if (argc < 4) {
		printf("format is %s <bus>, <dev>\n", argv[0]);
		printf("example %s mlxdevm auxiliary mlx_core.sf.3\n", argv[0]);
		printf("example %s devlink pci 0000:03:00.0\n", argv[0]);
		return EINVAL;
	}

	dl = mlxdevm_open(argv[1], argv[2], argv[3]);
	if (!dl) {
		fprintf(stderr, "%s fail to connect to %s %d\n", __func__, argv[1], errno);
		return errno;
	}

	set_params(dl);
	return 0;
}
