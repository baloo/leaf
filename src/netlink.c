
#include <stdlib.h>
#include <net/if_arp.h>

#pragma GCC diagnostic push
#ifdef __clang__
#pragma GCC diagnostic ignored "-Wpedantic"
#else
#pragma GCC diagnostic ignored "-pedantic"
#endif

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wlong-long"
#include <netlink/netlink.h>
#pragma GCC diagnostic pop

#include <netlink/route/link.h>
#include <netlink/msg.h>
#pragma GCC diagnostic pop

#include <netlink/socket.h>
#include <linux/if.h>

#include "netlink.h"

enum { UNKNOWN,
       NOTPRESENT,
       DOWN,
       LOWERLAYERDOWN,
       TESTING,
       DORMANT,
       UP,
       IFLA_OPERSTATE_MAX } oper_states;

struct leaf_netlink {
	struct nl_sock *nl_sk;
	leaf_netlink_cb *cb;
	void *data;
};

int
leaf_netlink_recv(struct leaf_netlink *ln)
{
	if (ln != NULL)
		return nl_recvmsgs_default(ln->nl_sk);
	return -1;
}

int
leaf_netlink_fd(struct leaf_netlink *ln)
{
	if (ln != NULL)
		return nl_socket_get_fd(ln->nl_sk);

	return -1;
}

int
leaf_netlink_ifaces_set(struct leaf_netlink *ln, int ifaces_n, char **ifaces,
			state_t state)
{
	struct rtnl_link *link;
	int ret = 0, i, err;
	struct nl_cache *cache;

	/* Get cache from netlink */
	if ((err = rtnl_link_alloc_cache(ln->nl_sk, AF_UNSPEC, &cache)) < 0) {
		ret = -1;
		goto free_cache;
	}

	for (i = 0; i < ifaces_n; i++) {
		link = rtnl_link_get_by_name(cache, ifaces[i]);
		if (link != NULL) {
			switch (state) {
			case LINK_DOWN:
				rtnl_link_unset_flags(link, IFF_UP);
				break;
			case LINK_UP:
				rtnl_link_set_flags(link, IFF_UP);
				break;
			default:
				continue;
			}
			rtnl_link_change(ln->nl_sk, link, link, 0);
		}
	}

free_cache:
	nl_cache_free(cache);
	return ret;
}

static void
netlink_msg_parsed(struct nl_object *o, void *arg)
{
	struct leaf_netlink *ln = arg;
	struct rtnl_link *link = (struct rtnl_link *)o;
	state_t simple_state;

	switch (rtnl_link_get_operstate(link)) {
	case UP:
		simple_state = LINK_UP;
		break;
	case DOWN:
	case LOWERLAYERDOWN:
		simple_state = LINK_DOWN;
		break;
	default:
		simple_state = LINK_UNKNOWN;
		break;
	}

	(ln->cb)(rtnl_link_get_name(link), simple_state, ln->data);
}

/*
 * This function will be called for each valid netlink message received
 * in nl_recvmsgs_default()
 */
static int
leaf_netlink_notify_link(struct nl_msg *msg, void *arg)
{
	const struct nlmsghdr *msg_hdr;

	msg_hdr = nlmsg_hdr(msg);
	{
		switch (msg_hdr->nlmsg_type) {
		case RTM_SETLINK:
		case RTM_NEWLINK:
		case RTM_DELLINK: {
			nl_msg_parse(msg, netlink_msg_parsed, arg);
			return 0;
		}
		}
	}

	return 0;
}

int
leaf_netlink_ifaces_exists(struct leaf_netlink *ln, char *const ifaces[],
			   int max)
{
	int err, i;
	struct nl_cache *cache;

	/* Get cache from netlink */
	if ((err = rtnl_link_alloc_cache(ln->nl_sk, AF_UNSPEC, &cache)) < 0) {
		nl_perror(err, "Unable to allocate cache");
		return -2;
	}

	err = 1;

	/* Look into cache */
	for (i = 0; i < max; i++) {
		if (rtnl_link_get_by_name(cache, ifaces[i]) == NULL) {
			fprintf(stderr, "err: %s is not a valid iface\n",
				ifaces[i]);
			err = -1;
		}
	}

	nl_cache_free(cache);

	return err;
}

static void
netlink_setup(struct leaf_netlink *ln)
{
	/*
	 * Notifications do not use sequence numbers, disable sequence number
	 * checking.
	 */
	nl_socket_disable_seq_check(ln->nl_sk);

	/*
	 * Define a callback function, which will be called for each
	 * notification
	 * received
	 */
	nl_socket_modify_cb(ln->nl_sk, NL_CB_VALID, NL_CB_CUSTOM,
			    leaf_netlink_notify_link, (void *)ln);

	/* Connect to routing netlink protocol */
	nl_connect(ln->nl_sk, NETLINK_ROUTE);

	/* Subscribe to link notifications group */
	nl_socket_add_memberships(ln->nl_sk, RTNLGRP_LINK, 0);

	/* Avoid leaf_netlink_recv() to block */
	nl_socket_set_nonblocking(ln->nl_sk);
}

int
leaf_netlink_create(struct leaf_netlink **ln_p, leaf_netlink_cb *cb, void *data)
{
	int err = 0;
	struct leaf_netlink *ln;

	ln = malloc(sizeof(struct leaf_netlink));
	if (ln == NULL) {
		return -1;
	}
	bzero(ln, sizeof(struct leaf_netlink));

	/* Allocate a new socket */
	ln->nl_sk = nl_socket_alloc();
	if (ln->nl_sk == NULL) {
		err = -1;
		goto leaf_netlink_create_free;
	}

	ln->cb = cb;
	ln->data = data;

	if ((err = nl_connect(ln->nl_sk, NETLINK_ROUTE)) < 0) {
		nl_perror(err, "Unable to connect socket");
		goto leaf_netlink_create_socket_free;
	}

	if (cb != NULL)
		netlink_setup(ln);

	*ln_p = ln;
	return 0;

leaf_netlink_create_socket_free:
	if (ln->nl_sk != NULL)
		nl_socket_free(ln->nl_sk);

leaf_netlink_create_free:
	free(ln);

	*ln_p = NULL;
	return err;
}

void
leaf_netlink_free(struct leaf_netlink *ln)
{
	if (ln == NULL)
		return;

	if (ln->nl_sk != NULL) {
		nl_socket_free(ln->nl_sk);
		ln->nl_sk = NULL;
	}

	free(ln);
	ln = NULL;
}
