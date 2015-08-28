
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
};

struct loop_cb {
	leaf_netlink_cb *cb;
	void *data;
};

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
		fprintf(stderr, "debug: changing iface %s\n", ifaces[i]);
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
/*
 * This function will be called for each valid netlink message received
 * in nl_recvmsgs_default()
 */

static int
leaf_netlink_notify_link(struct nl_msg *msg, void *arg)
{
	const struct nlmsghdr *msg_hdr;

	struct loop_cb *data = arg;

	msg_hdr = nlmsg_hdr(msg);
	{
		switch (msg_hdr->nlmsg_type) {
		case RTM_SETLINK:
		case RTM_NEWLINK:
		case RTM_DELLINK: {
			struct ifinfomsg *ifi = nlmsg_data(msg_hdr);
			struct rtattr *attribute;
			int len = msg_hdr->nlmsg_len -
				  NLMSG_LENGTH(sizeof(struct ifinfomsg));
			const char *ifname = "";
			__u8 state = 0;
			state_t simple_state;

			for (attribute = IFLA_RTA(ifi); RTA_OK(attribute, len);
			     attribute = RTA_NEXT(attribute, len)) {
				switch (attribute->rta_type) {
				case IFLA_IFNAME:
					ifname = RTA_DATA(attribute);
					break;
				case IFLA_OPERSTATE:
					state = *(__u8 *)RTA_DATA(attribute);
					break;
				}
			}

			if (strcmp(ifname, "") == 0)
				return -1;
			if (state == 0)
				return -2;

			if (state >= IFLA_OPERSTATE_MAX) {
				fprintf(stderr,
					"fatal: unhandled IFLA_OPERSTATE %#x\n",
					state);
				fprintf(stderr, "fatal: you should check your "
						"kernel version for new "
						"operstate and upgrade leaf "
						"accordingly\n");
				exit(1);
			}
			simple_state = LINK_UNKNOWN;
			if (state == UP) {
				simple_state = LINK_UP;
			} else if (state == DOWN || state == LOWERLAYERDOWN) {
				simple_state = LINK_DOWN;
			} else {
				simple_state = LINK_UNKNOWN;
			}

			(*data->cb)(ifname, simple_state, data->data);
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

int
leaf_netlink_create(struct leaf_netlink **ln_p)
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

	if ((err = nl_connect(ln->nl_sk, NETLINK_ROUTE)) < 0) {
		nl_perror(err, "Unable to connect socket");
		goto leaf_netlink_create_socket_free;
	}

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

int
leaf_netlink_main(struct leaf_netlink *ln, leaf_netlink_cb *cb, void *data)
{
	struct loop_cb args;

	if (ln == NULL)
		return -1;
	if (ln->nl_sk == NULL)
		return -2;

	args.cb = cb;
	args.data = data;

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
			    leaf_netlink_notify_link, (void *)&args);

	/* Connect to routing netlink protocol */
	nl_connect(ln->nl_sk, NETLINK_ROUTE);

	/* Subscribe to link notifications group */
	nl_socket_add_memberships(ln->nl_sk, RTNLGRP_LINK, 0);

	/*
	 * Start receiving messages. The function nl_recvmsgs_default() will
	 * block
	 * until one or more netlink messages (notification) are received which
	 * will be passed on to leaf_netlink_notify_link().
	 */

	while (1) {
		nl_recvmsgs_default(ln->nl_sk);
	}
}
