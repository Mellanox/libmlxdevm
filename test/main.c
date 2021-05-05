#include <mlxdevm_netlink.h>
#include <mlxdevm.h>

int main(int argc, char **argv)
{
	uint8_t macaddr[6] = { 0x0, 0x11, 0x22, 0x33, 0x44, 0x55 };
	struct mlxdevm_port *port;
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

	return 0;

mac_err:
	mlxdevm_sf_port_del(dl, port);

add_err:
	mlxdevm_close(dl);
	return err;
}