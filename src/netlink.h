#include "common.h"

struct leaf_netlink;

typedef void(leaf_netlink_cb)(const char *ifname, state_t new_state,
			      void *data);

int leaf_netlink_fd(struct leaf_netlink *ln);
int leaf_netlink_recv(struct leaf_netlink *ln);
int leaf_netlink_ifaces_set(struct leaf_netlink *ln, int ifaces_n,
			    char **ifaces, state_t state);

int leaf_netlink_create(struct leaf_netlink **ln_p, leaf_netlink_cb *cb,
			void *data);
void leaf_netlink_free(struct leaf_netlink *ln);
int leaf_netlink_ifaces_exists(struct leaf_netlink *ln, char *const ifaces[],
			       int max);
