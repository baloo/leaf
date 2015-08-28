#include "common.h"

struct leaf_lldp;
typedef void (*leaf_lldp_callback)(struct leaf_lldp *ll, const char *ifname,
				   const char *remote_ifname,
				   const char *remote_fqdn, state_t new_state,
				   void *data);

int leaf_lldp_fd(struct leaf_lldp *ll);
int leaf_lldp_recv(struct leaf_lldp *ll);
int leaf_lldp_create(const char *ctlname, struct leaf_lldp **ll_p,
		     leaf_lldp_callback cb, void *data);
void leaf_lldp_free(struct leaf_lldp *ll);
