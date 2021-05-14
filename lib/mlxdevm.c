#include <stddef.h>
#include "mlxdevm_netlink.h"
#include <time.h>
#include <errno.h>    

#include "mlxdevm.h"

void mlxdevm_close(struct mlxdevm *dl)
{
	mnlu_gen_socket_close(&dl->nlg);
	if (dl->bus)
		free(dl->bus);
	if (dl->dev)
		free(dl->dev);
	free(dl);
}

struct mlxdevm *mlxdevm_open(const char *dl_sock_name, const char *dl_bus, const char *dl_dev)
{
	struct mlxdevm *dl;
	int err;

	dl = calloc(1, sizeof(*dl));
	if (!dl)
		return NULL;

	err = mnlu_gen_socket_open(&dl->nlg, dl_sock_name,
				   MLXDEVM_GENL_VERSION);
	if (err) {
		fprintf(stderr, "Failed to connect to mlxdevm Netlink %d\n", errno);
		goto sock_err;
	}
	dl->bus = strdup(dl_bus);
	dl->dev = strdup(dl_dev);
	if (!dl->bus || !dl->dev)
		goto str_err;

	return dl;

str_err:
	mnlu_gen_socket_close(&dl->nlg);
	if (dl->bus)
		free(dl->bus);
	if (dl->dev)
		free(dl->dev);
sock_err:
	free(dl);
	return NULL;
}

static const enum mnl_attr_data_type mlxdevm_policy[MLXDEVM_ATTR_MAX + 1] = {
	[MLXDEVM_ATTR_DEV_BUS_NAME] = MNL_TYPE_NUL_STRING,
	[MLXDEVM_ATTR_DEV_NAME] = MNL_TYPE_NUL_STRING,
	[MLXDEVM_ATTR_PORT_INDEX] = MNL_TYPE_U32,
	[MLXDEVM_ATTR_PORT_TYPE] = MNL_TYPE_U16,
	[MLXDEVM_ATTR_PORT_NETDEV_IFINDEX] = MNL_TYPE_U32,
	[MLXDEVM_ATTR_PORT_NETDEV_NAME] = MNL_TYPE_NUL_STRING,
	[MLXDEVM_ATTR_PORT_IBDEV_NAME] = MNL_TYPE_NUL_STRING,
	[MLXDEVM_ATTR_PARAM_NAME] = MNL_TYPE_STRING,
	[MLXDEVM_ATTR_PARAM_TYPE] = MNL_TYPE_U8,
	[MLXDEVM_ATTR_PARAM_VALUES_LIST] = MNL_TYPE_NESTED,
	[MLXDEVM_ATTR_PARAM_VALUE] = MNL_TYPE_NESTED,
	[MLXDEVM_ATTR_PARAM_VALUE_CMODE] = MNL_TYPE_U8,
};

static int attr_cb(const struct nlattr *attr, void *data)
{
	const struct nlattr **tb = data;
	int type;

	if (mnl_attr_type_valid(attr, MLXDEVM_ATTR_MAX) < 0)
		return MNL_CB_OK;

	type = mnl_attr_get_type(attr);
	if (mnl_attr_validate(attr, mlxdevm_policy[type]) < 0)
		return MNL_CB_ERROR;

	tb[type] = attr;
	return MNL_CB_OK;
}

static const enum mnl_attr_data_type
mlxdevm_function_policy[MLXDEVM_PORT_FUNCTION_ATTR_MAX + 1] = {
	[MLXDEVM_PORT_FUNCTION_ATTR_HW_ADDR ] = MNL_TYPE_BINARY,
	[MLXDEVM_PORT_FN_ATTR_STATE] = MNL_TYPE_U8,
	[MLXDEVM_PORT_FN_ATTR_EXT_CAP_ROCE] = MNL_TYPE_U8,
        [MLXDEVM_PORT_FN_ATTR_EXT_CAP_UC_LIST] = MNL_TYPE_U32,
};

static int function_attr_cb(const struct nlattr *attr, void *data)
{
	const struct nlattr **tb = data;
	int type;

	/* Allow the tool to work on top of newer kernels that might contain
	 * more attributes.
	 */
	if (mnl_attr_type_valid(attr, MLXDEVM_PORT_FUNCTION_ATTR_MAX) < 0)
		return MNL_CB_OK;

	type = mnl_attr_get_type(attr);
	if (mnl_attr_validate(attr, mlxdevm_function_policy[type]) < 0)
		return MNL_CB_ERROR;

	tb[type] = attr;
	return MNL_CB_OK;
}

static void cmd_port_fn_get(struct nlattr **tb_port, struct mlxdevm_port *port)
{
	struct nlattr *tb[MLXDEVM_PORT_FUNCTION_ATTR_MAX + 1] = {};
	int err;

	err = mnl_attr_parse_nested(tb_port[MLXDEVM_ATTR_PORT_FUNCTION],
				    function_attr_cb, tb);
	if (err != MNL_CB_OK)
		return;

	port->state = mnl_attr_get_u8(tb[MLXDEVM_PORT_FN_ATTR_STATE]);
	port->opstate = mnl_attr_get_u8(tb[MLXDEVM_PORT_FN_ATTR_OPSTATE]);

	if (tb[MLXDEVM_PORT_FN_ATTR_EXT_CAP_ROCE]) {
		port->ext_cap.roce =
			mnl_attr_get_u8(tb[MLXDEVM_PORT_FN_ATTR_EXT_CAP_ROCE]);
		port->ext_cap.roce_valid = true;
	}
	if (tb[MLXDEVM_PORT_FN_ATTR_EXT_CAP_UC_LIST]) {
		port->ext_cap.max_uc_macs =
			mnl_attr_get_u32(tb[MLXDEVM_PORT_FN_ATTR_EXT_CAP_UC_LIST]);
		port->ext_cap.max_uc_macs_valid = true;
	}
}

static int cmd_port_show_cb(const struct nlmsghdr *nlh, void *data)
{
	struct genlmsghdr *genl = mnl_nlmsg_get_payload(nlh);
	struct nlattr *tb[MLXDEVM_ATTR_MAX + 1] = {};
	struct mlxdevm_port *port = data;

	mnl_attr_parse(nlh, sizeof(*genl), attr_cb, tb);
	if (!tb[MLXDEVM_ATTR_DEV_BUS_NAME] || !tb[MLXDEVM_ATTR_DEV_NAME] ||
	    !tb[MLXDEVM_ATTR_PORT_INDEX])
		return MNL_CB_ERROR;

	port->port_index = mnl_attr_get_u32(tb[MLXDEVM_ATTR_PORT_INDEX]);
	port->ndev_ifindex = mnl_attr_get_u32(tb[MLXDEVM_ATTR_PORT_NETDEV_IFINDEX]);
	cmd_port_fn_get(tb, port);
	return MNL_CB_OK;
}

struct mlxdevm_port *
mlxdevm_sf_port_add(struct mlxdevm *dl, uint32_t pfnum, uint32_t sfnum)
{
	struct mlxdevm_port *port;
	struct nlmsghdr *nlh;
	int err;

	port = calloc(1, sizeof(*port));
	if (!port)
		return NULL;

	port->pfnum = pfnum;
	port->sfnum = sfnum;

	nlh = mnlu_gen_socket_cmd_prepare(&dl->nlg, MLXDEVM_CMD_PORT_NEW,
					  NLM_F_REQUEST | NLM_F_ACK);

	mnl_attr_put_strz(nlh, MLXDEVM_ATTR_DEV_BUS_NAME, dl->bus);
	mnl_attr_put_strz(nlh, MLXDEVM_ATTR_DEV_NAME, dl->dev);

	mnl_attr_put_u16(nlh, MLXDEVM_ATTR_PORT_FLAVOUR, MLXDEVM_PORT_FLAVOUR_PCI_SF);
	mnl_attr_put_u16(nlh, MLXDEVM_ATTR_PORT_PCI_PF_NUMBER, pfnum);
	mnl_attr_put_u32(nlh, MLXDEVM_ATTR_PORT_PCI_SF_NUMBER, sfnum);

	err = mnlu_gen_socket_sndrcv(&dl->nlg, nlh, cmd_port_show_cb, port);
	if (err)
		goto sock_err;

	return port;

sock_err:
	free(port);
	return NULL;
}

static void port_handle_set(struct nlmsghdr *nlh, const struct mlxdevm *dl,
			    const struct mlxdevm_port *port)
{
	mnl_attr_put_strz(nlh, MLXDEVM_ATTR_DEV_BUS_NAME, dl->bus);
	mnl_attr_put_strz(nlh, MLXDEVM_ATTR_DEV_NAME, dl->dev);
	mnl_attr_put_u32(nlh, MLXDEVM_ATTR_PORT_INDEX, port->port_index);
}

void mlxdevm_sf_port_del(struct mlxdevm *dl, struct mlxdevm_port *port)
{
	struct nlmsghdr *nlh;
	int err;

	nlh = mnlu_gen_socket_cmd_prepare(&dl->nlg, MLXDEVM_CMD_PORT_DEL,
					  NLM_F_REQUEST | NLM_F_ACK);

	port_handle_set(nlh, dl, port);

	err = mnlu_gen_socket_sndrcv(&dl->nlg, nlh, NULL, NULL);
	if (err)
		return;

	free(port);
}

static void port_fn_mac_addr_put(struct nlmsghdr *nlh, const uint8_t *addr)
{
	struct nlattr *nest;

	nest = mnl_attr_nest_start(nlh, MLXDEVM_ATTR_PORT_FUNCTION);

	mnl_attr_put(nlh, MLXDEVM_PORT_FUNCTION_ATTR_HW_ADDR, 6, addr);
	mnl_attr_nest_end(nlh, nest);
}

int mlxdevm_port_fn_macaddr_set(struct mlxdevm *dl, struct mlxdevm_port *port,
				const uint8_t *addr)
{
	struct nlmsghdr *nlh;
	int err;

	nlh = mnlu_gen_socket_cmd_prepare(&dl->nlg, MLXDEVM_CMD_PORT_SET,
					  NLM_F_REQUEST | NLM_F_ACK);
	port_handle_set(nlh, dl, port);
	port_fn_mac_addr_put(nlh, addr);

	err = mnlu_gen_socket_sndrcv(&dl->nlg, nlh, NULL, NULL);
	if (err)
		return err;
	memcpy(port->mac_addr, addr, sizeof(port->mac_addr));
	return 0;
}

static void port_fn_state_put(struct nlmsghdr *nlh, uint8_t state)
{
	struct nlattr *nest;

	nest = mnl_attr_nest_start(nlh, MLXDEVM_ATTR_PORT_FUNCTION);

	mnl_attr_put_u8(nlh, MLXDEVM_PORT_FN_ATTR_STATE, state);
	mnl_attr_nest_end(nlh, nest);
}

int mlxdevm_port_fn_state_set(struct mlxdevm *dl, struct mlxdevm_port *port,
			      uint8_t state)
{
	struct nlmsghdr *nlh;
	int err;

	nlh = mnlu_gen_socket_cmd_prepare(&dl->nlg, MLXDEVM_CMD_PORT_SET,
					  NLM_F_REQUEST | NLM_F_ACK);
	port_handle_set(nlh, dl, port);
	port_fn_state_put(nlh, state);
	err = mnlu_gen_socket_sndrcv(&dl->nlg, nlh, NULL, NULL);
	if (err)
		return err;

	port->state = state;
	return 0;
}

int mlxdevm_port_fn_state_get(struct mlxdevm *dl, struct mlxdevm_port *port,
			      uint8_t *state, uint8_t *opstate)
{
	struct nlmsghdr *nlh;
	int err;

	nlh = mnlu_gen_socket_cmd_prepare(&dl->nlg, MLXDEVM_CMD_PORT_GET,
					  NLM_F_REQUEST | NLM_F_ACK);

	port_handle_set(nlh, dl, port);
	err = mnlu_gen_socket_sndrcv(&dl->nlg, nlh, cmd_port_show_cb, port);
	if (err)
		return err;

	*state = port->state;
	*opstate = port->opstate;
	return 0;
}

static int cmd_netdev_get_cb(const struct nlmsghdr *nlh, void *data)
{
	struct genlmsghdr *genl = mnl_nlmsg_get_payload(nlh);
	struct nlattr *tb[MLXDEVM_ATTR_MAX + 1] = {};
	const char *ifname = NULL;

	mnl_attr_parse(nlh, sizeof(*genl), attr_cb, tb);
	if (!tb[MLXDEVM_ATTR_DEV_BUS_NAME] || !tb[MLXDEVM_ATTR_DEV_NAME] ||
	    !tb[MLXDEVM_ATTR_PORT_INDEX] ||
	    !tb[MLXDEVM_ATTR_PORT_NETDEV_NAME])
		return MNL_CB_ERROR;

	ifname = mnl_attr_get_str(tb[MLXDEVM_ATTR_PORT_NETDEV_NAME]);
	strcpy(data, ifname);
	return MNL_CB_OK;
}

int mlxdevm_port_netdev_get(struct mlxdevm *dl, struct mlxdevm_port *port,
			    char *ifname)
{
	struct nlmsghdr *nlh;
	int err;

	nlh = mnlu_gen_socket_cmd_prepare(&dl->nlg, MLXDEVM_CMD_PORT_GET,
					  NLM_F_REQUEST | NLM_F_ACK |
					  NLM_F_DUMP);

	port_handle_set(nlh, dl, port);
	err = mnlu_gen_socket_sndrcv(&dl->nlg, nlh, cmd_netdev_get_cb, ifname);
	return err;
}

static int msleep(long msec)
{
	struct timespec ts;
	int res;
	
	ts.tv_sec = msec / 1000;
	ts.tv_nsec = (msec % 1000) * 1000000;
	
	do {
		res = nanosleep(&ts, &ts);
	} while (res && errno == EINTR);
	
	return res;
}

int mlxdevm_port_fn_opstate_wait_attached(struct mlxdevm *dl,
					  struct mlxdevm_port *port)
{
	int count = 4000; /* 400msec timeout */
	uint8_t opstate;
	uint8_t state;
	int err;

	while (count) {
		err = mlxdevm_port_fn_state_get(dl, port, &state, &opstate);
		if (err)
			return err;
		if (opstate == MLXDEVM_PORT_FN_OPSTATE_ATTACHED)
			return 0;

		msleep(1);
		count--;
	}
	return EINVAL;
}

static void port_fn_ext_cap_put(struct nlmsghdr *nlh,
				const struct mlxdevm_port_fn_ext_cap *cap)
{
	struct nlattr *nest;

	nest = mnl_attr_nest_start(nlh, MLXDEVM_ATTR_EXT_PORT_FN_CAP);

	if (cap->roce_valid)
		mnl_attr_put_u8(nlh, MLXDEVM_PORT_FN_ATTR_EXT_CAP_ROCE, cap->roce);
	if (cap->max_uc_macs_valid)
		mnl_attr_put_u32(nlh, MLXDEVM_PORT_FN_ATTR_EXT_CAP_UC_LIST,
				 cap->max_uc_macs);

	mnl_attr_nest_end(nlh, nest);
}

int mlxdevm_port_fn_cap_set(struct mlxdevm *dl, struct mlxdevm_port *port,
			    const struct mlxdevm_port_fn_ext_cap *cap)
{
	struct nlmsghdr *nlh;
	int err;

	if (!port->ext_cap.roce_valid && !port->ext_cap.max_uc_macs_valid)
		return -EOPNOTSUPP;

	nlh = mnlu_gen_socket_cmd_prepare(&dl->nlg, MLXDEVM_CMD_EXT_CAP_SET,
					  NLM_F_REQUEST | NLM_F_ACK);
	port_handle_set(nlh, dl, port);
	port_fn_ext_cap_put(nlh, cap);
	err = mnlu_gen_socket_sndrcv(&dl->nlg, nlh, NULL, NULL);
	if (err)
		return err;

	if (cap->roce_valid)
		port->ext_cap.roce = cap->roce;
	if (cap->max_uc_macs_valid)
		port->ext_cap.max_uc_macs = cap->max_uc_macs;
	return 0;
}
