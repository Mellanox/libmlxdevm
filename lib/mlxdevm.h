#ifndef _MLXDEVM_H_
#define _MLXDEVM_H_

#include <errno.h>
#include <string.h>
#include <time.h>
#include <libmnl/libmnl.h>
#include <linux/genetlink.h>

#include "libnetlink.h"
#include "mnl_utils.h"
#include "utils.h"

struct mlxdevm {
	struct mnlu_gen_socket nlg;
	char *bus;
	char *dev;
};

/**
 * mlxdevm_connect - Connect to the mlxdevm socket of kernel.
 * 
 * @dl_bus: mlxdevm instance bus name such as pci
 * @dl_dev: mlxdevm instance device name such as 0000:03:00.0
 *
 * Connect to mlxdevm socket in kernel communication. On success
 * it returns valid handle or returns NULL on error.
 */
struct mlxdevm *mlxdevm_open(const char *dl_sock_name,
			     const char *dl_bus, const char *dl_dev);

/**
 * mlxdevm_close - Close a previously open mlxdevm connection.
 * 
 * Close a previously opened mlxdevm socket and frees the associated
 * memory.
 */
void mlxdevm_close(struct mlxdevm *dl);

struct mlxdevm_port_fn_ext_cap {
	uint32_t max_uc_macs;
	uint8_t roce;
	uint8_t max_uc_macs_valid : 1;
	uint8_t roce_valid : 1;
};

/**
 * mlxdevm_port - mlxdevm port to be allocated by port allocation API
 * @port_index: port index assignend by the kernel for this port.
 */
struct mlxdevm_port {
	uint32_t ndev_ifindex;
	uint32_t port_index;
	uint32_t pfnum;
	uint32_t sfnum;
	uint8_t mac_addr[6];
	uint8_t state;
	uint8_t opstate;
	struct mlxdevm_port_fn_ext_cap ext_cap;
};

/**
 * mlxdevm_sf_port_add - Add mlxdevm SF port
 *
 * Add mlxdevm SF port for specified PCI PF and SF number.
 * It returns valid mlxdevm port on success or NULL on error.
 */
struct mlxdevm_port *
mlxdevm_sf_port_add(struct mlxdevm *dl, uint32_t pfnum, uint32_t sfnum);

/**
 * mlxdevm_sf_port_del - Deleted previously created SF port
 */
void mlxdevm_sf_port_del(struct mlxdevm *dl, struct mlxdevm_port *port);

/**
 * mlxdevm_port_fn_mac_addr_set - Set the mlxdevm port's mac address
 */
int mlxdevm_port_fn_macaddr_set(struct mlxdevm *dl, struct mlxdevm_port *port,
				const uint8_t *addr);

int mlxdevm_port_fn_state_set(struct mlxdevm *dl, struct mlxdevm_port *port,
			      uint8_t state);

int mlxdevm_port_fn_state_get(struct mlxdevm *dl, struct mlxdevm_port *port,
			      uint8_t *state, uint8_t *opstate);

int mlxdevm_port_netdev_get(struct mlxdevm *dl, struct mlxdevm_port *port,
			    char *ifname);

/**
 * mlxdevm_port_fn_opstate_wait_attached - Wait for port function operational
 * state to become active. Caller must first active the port function.
 */
int mlxdevm_port_fn_opstate_wait_attached(struct mlxdevm *dl,
					  struct mlxdevm_port *port);


/**
 * mlxdevm_port_fn_cap_set - Set optional function capabilities if it is
 * supported. Each capability has a value and a valid bit mask. Caller
 * must set valid bit of a capability to set/clear a value. Multiple
 * capabilities can be set/clear with single call. Port capabilities are
 * updated on successuful execution.
 * Caller must invoke this API before activating the SF port function to
 * active state.
 * Return: 0 on success or error code. When port doesn't have capability
 * exposed, API return error code -EOPNOTSUPP.
 */
int mlxdevm_port_fn_cap_set(struct mlxdevm *dl, struct mlxdevm_port *port,
			    const struct mlxdevm_port_fn_ext_cap *cap);

struct mlxdevm_param {
	uint8_t cmode;
	uint8_t nla_type;
	union {
		uint8_t val_u8;
		uint16_t val_u16;
		uint32_t val_u32;
		bool val_bool;
	} u;
};

/**
 * mlxdevm_dev_driver_param_get - Get a parameter fields for a specific
 * parameter of the device.
 * Return: 0 on success along with param fields or error error code.
 */
int mlxdevm_dev_driver_param_get(struct mlxdevm *dl, const char *param_name,
				 struct mlxdevm_param *param);

/**
 * mlxdevm_dev_driver_param_set - Set a specified parameter by its name for
 * a specified device.
 * Return: 0 on success or error code.
 */
int mlxdevm_dev_driver_param_set(struct mlxdevm *dl, const char *param_name,
				 const struct mlxdevm_param *param);

#endif
