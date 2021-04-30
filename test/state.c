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

int main(int argc, char **argv)
{
	uint8_t macaddr[6] = { 0x0, 0x11, 0x22, 0x33, 0x44, 0x55 };
	struct mlxdevm_port_fn_ext_cap cap = {};
	struct mlxdevm_port *port;
	char ifname[64] = {};
	struct mlxdevm *dl;
	uint8_t opstate;
	uint8_t state;
	int err;

	if (argc < 4) {
		printf("format is %s <bus>, <dev>\n", argv[0]);
		printf("example %s mlxdevm pci 0000:03:00.0\n", argv[0]);
		printf("example %s devlink pci 0000:03:00.0\n", argv[0]);
		return EINVAL;
	}

	dl = mlxdevm_open(argv[1], argv[2], argv[3]);
	if (!dl) {
		fprintf(stderr, "%s fail to connect to mlxdevm %d\n", __func__, errno);
		return errno;
	}

	port = mlxdevm_sf_port_add(dl, 0, 99);
	if (!port) {
		fprintf(stderr, "%s sf port add fail %d\n", __func__, errno);
		err = errno;
		goto add_err;
	}
	printf("port added port index = %d, netdef ifindex = %d\n",
		port->port_index, port->ndev_ifindex);
	printf("roce cap = %d mac_uc_macs = %d\n",
	       port->ext_cap.roce, port->ext_cap.max_uc_macs);
	printf("valid roce = %d, uc list = %d\n",
	       port->ext_cap.roce_valid, port->ext_cap.max_uc_macs_valid);

	if (port->ext_cap.roce_valid || port->ext_cap.max_uc_macs_valid) {
		cap.roce = false;
		cap.roce_valid = port->ext_cap.roce_valid;

		cap.max_uc_macs = 1;
		cap.max_uc_macs_valid = port->ext_cap.max_uc_macs_valid;

		err = mlxdevm_port_fn_cap_set(dl, port, &cap);
		printf("err = %d\n", err);
	}

	err = mlxdevm_port_fn_macaddr_set(dl, port, macaddr);
	if (err) {
		fprintf(stderr, "%s fail to set sf mac %d\n", __func__, err);
		goto mac_err;
	}

	state = MLXDEVM_PORT_FN_STATE_ACTIVE;
	err = mlxdevm_port_fn_state_set(dl, port, state);
	if (err) {
		fprintf(stderr, "%s fail to activate sf %d\n", __func__, err);
		goto mac_err;
	}

	err = mlxdevm_port_fn_state_get(dl, port, &state, &opstate);
	if (err)
		goto mac_err;

	err = mlxdevm_port_fn_opstate_wait_attached(dl, port);
	if (err) {
		fprintf(stderr, "%s fail to see device attached %d\n", __func__, err);
	}

	err = mlxdevm_port_netdev_get(dl, port, ifname);
	if (err)
		fprintf(stderr, "%s fail to get rep ifname %d\n",
			__func__, err);
	else
		printf("sf rep ifname = %s\n", ifname);

	state = MLXDEVM_PORT_FN_STATE_INACTIVE;
	err = mlxdevm_port_fn_state_set(dl, port, state);
	if (err) {
		fprintf(stderr, "%s fail to activate sf %d\n", __func__, err);
		goto mac_err;
	}

	err = mlxdevm_port_fn_opstate_wait_detached(dl, port);
	if (err) {
		fprintf(stderr, "%s fail to see device detached %d\n", __func__, err);
	}
	mlxdevm_sf_port_del(dl, port);

	port = mlxdevm_sf_port_add(dl, 0, 99);
	if (!port) {
		fprintf(stderr, "%s sf port add fail %d\n", __func__, errno);
		err = errno;
		goto add_err;
	}
	mlxdevm_sf_port_del(dl, port);

	return 0;

mac_err:
	mlxdevm_sf_port_del(dl, port);

add_err:
	mlxdevm_close(dl);
	return err;
}
