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

#include <errno.h>
#include <string.h>
#include <time.h>
#include <stdlib.h>
#include <libmnl/libmnl.h>
#include <linux/genetlink.h>

#include "netlink_utils.h"

#define ARRAY_SIZE(x) (sizeof(x) / sizeof((x)[0]))

static void ext_ack_msg_print(bool is_err, const char *msg)
{
	fprintf(stderr, "%s: %s", is_err ? "Error" : "Warning", msg);
	if (msg[strlen(msg) - 1] != '.')
		fprintf(stderr, ".");
	fprintf(stderr, "\n");
}

static const enum mnl_attr_data_type extack_policy[NLMSG_ERR_ATTR_MAX + 1] = {
	[NLMSG_ERR_ATTR_OFFS] = MNL_TYPE_U32,
	[NLMSG_ERR_ATTR_MSG] = MNL_TYPE_NUL_STRING,
};

static int err_attr_cb(const struct nlattr *attr, void *data)
{
	const struct nlattr **tb = data;
	uint16_t type;

	if (mnl_attr_type_valid(attr, NLMSG_ERR_ATTR_MAX) < 0) {
		fprintf(stderr, "Invalid extack attribute\n");
		return MNL_CB_ERROR;
	}

	type = mnl_attr_get_type(attr);
	if (mnl_attr_validate(attr, extack_policy[type]) < 0) {
		fprintf(stderr, "extack attribute %d failed validation\n",
			type);
		return MNL_CB_ERROR;
	}

	tb[type] = attr;
	return MNL_CB_OK;
}

static int netlink_ext_ack_dump(const struct nlmsghdr *nlh)
{
	const struct nlmsgerr *err = mnl_nlmsg_get_payload(nlh);
	struct nlattr *tb[NLMSG_ERR_ATTR_MAX + 1] = {};
	unsigned int hlen = sizeof(*err);
	const char *msg = NULL;
	int ret;

	/* no TLVs, nothing to do here */
	if (!(nlh->nlmsg_flags & NLM_F_ACK_TLVS))
		return 0;

	/* if NLM_F_CAPPED is set then the inner err msg was capped */
	if (!(nlh->nlmsg_flags & NLM_F_ACK_REQ_CAPPED))
		hlen += mnl_nlmsg_get_payload_len(&err->msg);

	ret = mnl_attr_parse(nlh, hlen, err_attr_cb, tb);
	if (ret != MNL_CB_OK)
		return 0;

	if (tb[NLMSG_ERR_ATTR_OFFS]) {
		uint32_t off = 0;

		off = mnl_attr_get_u32(tb[NLMSG_ERR_ATTR_OFFS]);
		if (off > nlh->nlmsg_len) {
			fprintf(stderr,
				"Invalid offset for NLMSG_ERR_ATTR_OFFS\n");
			off = 0;
		}
	}

	if (tb[NLMSG_ERR_ATTR_MSG])
		msg = mnl_attr_get_str(tb[NLMSG_ERR_ATTR_MSG]);

	if (msg && *msg != '\0') {
		bool is_err = !!err->error;

		ext_ack_msg_print(is_err, msg);
		return is_err ? 1 : 0;
	}

	return 0;
}

static int netlink_ext_ack_dump_done(const struct nlmsghdr *nlh, int error)
{
	struct nlattr *tb[NLMSG_ERR_ATTR_MAX + 1] = {};
	unsigned int hlen = sizeof(int);
	const char *msg = NULL;

	if (mnl_attr_parse(nlh, hlen, err_attr_cb, tb) != MNL_CB_OK)
		return 0;

	if (tb[NLMSG_ERR_ATTR_MSG])
		msg = mnl_attr_get_str(tb[NLMSG_ERR_ATTR_MSG]);

	if (msg && *msg != '\0') {
		bool is_err = !!error;

		ext_ack_msg_print(is_err, msg);
		return is_err ? 1 : 0;
	}

	return 0;
}

static struct mnl_socket *_netlink_socket_open(void)
{
	struct mnl_socket *nl;
	int one = 1;

	nl = mnl_socket_open(NETLINK_GENERIC);
	if (nl == NULL)
		return NULL;

	mnl_socket_setsockopt(nl, NETLINK_CAP_ACK, &one, sizeof(one));
	mnl_socket_setsockopt(nl, NETLINK_EXT_ACK, &one, sizeof(one));

	if (mnl_socket_bind(nl, 0, MNL_SOCKET_AUTOPID) < 0)
		goto err_bind;

	return nl;

err_bind:
	mnl_socket_close(nl);
	return NULL;
}

struct nlmsghdr *netlink_msg_prepare(void *buf, uint32_t nlmsg_type, uint16_t flags,
				  void *extra_header, size_t extra_header_size)
{
	struct nlmsghdr *nlh;
	void *eh;

	nlh = mnl_nlmsg_put_header(buf);
	nlh->nlmsg_type = nlmsg_type;
	nlh->nlmsg_flags = flags;
	nlh->nlmsg_seq = time(NULL);

	eh = mnl_nlmsg_put_extra_header(nlh, extra_header_size);
	memcpy(eh, extra_header, extra_header_size);
	return nlh;
}

static int noop_cb(const struct nlmsghdr *nlh, void *data)
{
	return MNL_CB_OK;
}

static int error_cb(const struct nlmsghdr *nlh, void *data)
{
	const struct nlmsgerr *err = mnl_nlmsg_get_payload(nlh);

	/* Netlink may return the errno value with different signess */
	if (err->error < 0)
		errno = -err->error;
	else
		errno = err->error;

	if (netlink_ext_ack_dump(nlh))
		return MNL_CB_ERROR;

	return err->error == 0 ? MNL_CB_STOP : MNL_CB_ERROR;
}

static int stop_cb(const struct nlmsghdr *nlh, void *data)
{
	int len = *(int *)NLMSG_DATA(nlh);

	if (len < 0) {
		errno = -len;
		netlink_ext_ack_dump_done(nlh, len);
		return MNL_CB_ERROR;
	}
	return MNL_CB_STOP;
}

static mnl_cb_t mnlu_cb_array[NLMSG_MIN_TYPE] = {
	[NLMSG_DONE]	= stop_cb,
	[NLMSG_NOOP]	= noop_cb,
	[NLMSG_ERROR]	= error_cb,
	[NLMSG_OVERRUN]	= noop_cb,
};

int netlink_socket_recv_run(struct mnl_socket *nl, unsigned int seq, void *buf,
			    size_t buf_size,
			    mnl_cb_t cb, void *data)
{
	unsigned int portid = mnl_socket_get_portid(nl);
	int err;

	do {
		err = mnl_socket_recvfrom(nl, buf, buf_size);
		if (err <= 0)
			break;
		err = mnl_cb_run2(buf, err, seq, portid,
				  cb, data, mnlu_cb_array,
				  ARRAY_SIZE(mnlu_cb_array));
	} while (err > 0);

	return err;
}

static int get_family_id_attr_cb(const struct nlattr *attr, void *data)
{
	int type = mnl_attr_get_type(attr);
	const struct nlattr **tb = data;

	if (mnl_attr_type_valid(attr, CTRL_ATTR_MAX) < 0)
		return MNL_CB_ERROR;

	if (type == CTRL_ATTR_FAMILY_ID &&
	    mnl_attr_validate(attr, MNL_TYPE_U16) < 0)
		return MNL_CB_ERROR;
	tb[type] = attr;
	return MNL_CB_OK;
}

static int get_family_id_cb(const struct nlmsghdr *nlh, void *data)
{
	struct genlmsghdr *genl = mnl_nlmsg_get_payload(nlh);
	struct nlattr *tb[CTRL_ATTR_MAX + 1] = {};
	uint32_t *p_id = data;

	mnl_attr_parse(nlh, sizeof(*genl), get_family_id_attr_cb, tb);
	if (!tb[CTRL_ATTR_FAMILY_ID])
		return MNL_CB_ERROR;
	*p_id = mnl_attr_get_u16(tb[CTRL_ATTR_FAMILY_ID]);
	return MNL_CB_OK;
}

static int family_get(struct netlink_socket *nls, const char *family_name)
{
	struct genlmsghdr hdr = {};
	struct nlmsghdr *nlh;
	int err;

	hdr.cmd = CTRL_CMD_GETFAMILY;
	hdr.version = 0x1;

	nlh = netlink_msg_prepare(nls->buf, GENL_ID_CTRL,
				  NLM_F_REQUEST | NLM_F_ACK,
				  &hdr, sizeof(hdr));

	mnl_attr_put_strz(nlh, CTRL_ATTR_FAMILY_NAME, family_name);

	err = mnl_socket_sendto(nls->nl, nlh, nlh->nlmsg_len);
	if (err < 0)
		return err;

	err = netlink_socket_recv_run(nls->nl, nlh->nlmsg_seq, nls->buf,
				   MNL_SOCKET_BUFFER_SIZE,
				   get_family_id_cb, &nls->family);
	return err;
}

int netlink_socket_open(struct netlink_socket *nls, const char *family_name,
			uint8_t version)
{
	int err;

	nls->buf = malloc(MNL_SOCKET_BUFFER_SIZE);
	if (!nls->buf)
		goto err_buf_alloc;

	nls->nl = _netlink_socket_open();
	if (!nls->nl)
		goto err_socket_open;

	err = family_get(nls, family_name);
	if (err)
		goto err_socket;

	return 0;

err_socket:
	mnl_socket_close(nls->nl);
err_socket_open:
	free(nls->buf);
err_buf_alloc:
	return -1;
}

void netlink_socket_close(struct netlink_socket *nls)
{
	mnl_socket_close(nls->nl);
	free(nls->buf);
}

struct nlmsghdr *
_netlink_socket_cmd_prepare(struct netlink_socket *nls,
			    uint8_t cmd, uint16_t flags,
			    uint32_t id, uint8_t version)
{
	struct genlmsghdr hdr = {};
	struct nlmsghdr *nlh;

	hdr.cmd = cmd;
	hdr.version = version;
	nlh = netlink_msg_prepare(nls->buf, id, flags, &hdr, sizeof(hdr));
	nls->seq = nlh->nlmsg_seq;
	return nlh;
}

struct nlmsghdr *netlink_socket_cmd_prepare(struct netlink_socket *nls,
					    uint8_t cmd, uint16_t flags)
{
	struct genlmsghdr hdr = {};
	struct nlmsghdr *nlh;

	hdr.cmd = cmd;
	hdr.version = nls->version;
	nlh = netlink_msg_prepare(nls->buf, nls->family, flags, &hdr, sizeof(hdr));
	nls->seq = nlh->nlmsg_seq;
	return nlh;
}

int netlink_socket_sndrcv(struct netlink_socket *nls, const struct nlmsghdr *nlh,
			  mnl_cb_t data_cb, void *data)
{
	int err;

	err = mnl_socket_sendto(nls->nl, nlh, nlh->nlmsg_len);
	if (err < 0) {
		perror("Failed to send data");
		return -errno;
	}

	err = netlink_socket_recv_run(nls->nl, nlh->nlmsg_seq, nls->buf,
				      MNL_SOCKET_BUFFER_SIZE,
				      data_cb, data);
	if (err < 0) {
		fprintf(stderr, "kernel answers: %s\n", strerror(errno));
		return -errno;
	}
	return 0;
}
