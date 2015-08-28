
#include <stdlib.h>
#include <strings.h>

#include <lldpctl.h>

#include "lldp.h"

struct leaf_lldp {
	lldpctl_conn_t *lldp;
	int loop;
};

void
leaf_lldp_stop(struct leaf_lldp *ll)
{
	ll->loop = 0;
}

int
leaf_lldp_main(struct leaf_lldp *ll)
{
	if (lldpctl_watch_callback(ll->lldp, NULL /* FIXME */, NULL) < 0) {
		/* FIXME log warn "unable to watch for neighbors. %s"
		 * lldpctl_last_strerror(ll->lldp) */
		return -1;
	}
	while (ll->loop) {
		if (lldpctl_watch(ll->lldp) < 0) {
			/* FIXME log warn "unable to watch for neighbors. %s"
			 * lldpctl_last_strerror(ll->lldp) */
			ll->loop = 0;
		}
	}
	return 0;
}

int
leaf_lldp_create(struct leaf_lldp **ll_p)
{
	int err;
	struct leaf_lldp *ll;

	ll = malloc(sizeof(struct leaf_lldp));
	if (ll == NULL) {
		return -1;
	}
	bzero(ll, sizeof(struct leaf_lldp));

	ll->loop = 1;

	ll->lldp = lldpctl_new(NULL, NULL, NULL);
	if (ll->lldp == NULL) {
		err = -1;
		goto free;
	}

	*ll_p = ll;
	return 0;

free:
	free(ll);
	return err;
}

void
leaf_lldp_free(struct leaf_lldp *ll)
{
	if (ll == NULL)
		return;

	if (ll->lldp != NULL) {
		lldpctl_release(ll->lldp);
		ll->lldp = NULL;
	}

	free(ll);
	ll = NULL;
}
