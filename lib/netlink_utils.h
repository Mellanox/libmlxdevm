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

#ifndef __NETLINK_UTILS_H__
#define __NETLINK_UTILS_H__ 1

enum nlmsg_err_attrs {
	NLMSG_ERR_ATTR_UNUSED,
	NLMSG_ERR_ATTR_MSG,
	NLMSG_ERR_ATTR_OFFS,
	NLMSG_ERR_ATTR_COOKIE,

	__NLMSG_ERR_ATTR_MAX,
	NLMSG_ERR_ATTR_MAX = __NLMSG_ERR_ATTR_MAX - 1
};

#ifndef NETLINK_CAP_ACK
#define NETLINK_CAP_ACK	10
#endif
#ifndef NETLINK_EXT_ACK
#define NETLINK_EXT_ACK	11
#endif

/* Netlink optional ACK message flags */
#define NLM_F_ACK_REQ_CAPPED	0x100	/* request was capped */
#define NLM_F_ACK_TLVS		0x200	/* extended ACK TLVs are included */

struct netlink_socket {
	char *buf;
	struct mnl_socket *nl;
	uint32_t family;
	unsigned int seq;
	uint8_t version;
};

int netlink_socket_open(struct netlink_socket *nlg, const char *family_name,
			 uint8_t version);
void netlink_socket_close(struct netlink_socket *nlg);

struct nlmsghdr *
_netlink_socket_cmd_prepare(struct netlink_socket *nlg,
			     uint8_t cmd, uint16_t flags,
			     uint32_t id, uint8_t version);
struct nlmsghdr *netlink_socket_cmd_prepare(struct netlink_socket *nlg,
					     uint8_t cmd, uint16_t flags);

int netlink_socket_sndrcv(struct netlink_socket *nlg, const struct nlmsghdr *nlh,
			   mnl_cb_t data_cb, void *data);

int mnlu_socket_recv_run(struct mnl_socket *nl, unsigned int seq, void *buf, size_t buf_size,
			 mnl_cb_t cb, void *data);

#endif /* __NETLINK_UTILS_H__ */
