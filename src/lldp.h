
struct leaf_lldp;

int leaf_lldp_main(struct leaf_lldp *ll);
int leaf_lldp_create(struct leaf_lldp **ll_p);
void leaf_lldp_free(struct leaf_lldp *ll);
void leaf_lldp_stop(struct leaf_lldp *ll);
