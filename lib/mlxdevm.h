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

struct mlxdevm_port;

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

/**
 * mlxdevm_port_fn_opstate_wait_attached - Wait for port function operational
 * state to become active. Caller must first active the port function.
 */
int mlxdevm_port_fn_opstate_wait_attached(struct mlxdevm *dl,
					  struct mlxdevm_port *port);

#endif
