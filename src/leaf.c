#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/errno.h>
#include <unistd.h>
#include <bsd/libutil.h>
#include <sys/types.h>
#include <sys/stat.h>

#include "netlink.h"
#include "lldp.h"

#define EXIT_FAILURE_INIT 2
#define EXIT_FAILURE_BAD_ARGUMENTS 3

#ifdef HAVE___PROGNAME
extern const char *__progname;
#else
#define __progname "leaf"
#endif

struct leaf_info {
	const char *upstream_iface;
	state_t upstream_state;
	struct leaf_netlink *ln_control;
	struct leaf_netlink *ln_watch;
	struct leaf_lldp *ll;
	int leaf_ifaces_n;
	char **leaf_ifaces;
};

volatile sig_atomic_t signal_received;

static void
termination_handler(__attribute__((unused)) int sig)
{
	signal_received++;
}

static void usage(void) __attribute__((noreturn));
static void
usage(void)
{
	fprintf(stderr, "Usage:   %s [OPTIONS ...] [LEAF IFACES ...]\n",
		__progname);
	fprintf(stderr, "-d          Enable more debugging information.\n");
	fprintf(stderr, "-f          Launch in foreground.\n");
	fprintf(stderr, "-u iface    Upstream interface.\n");
	fprintf(stderr, "-l logfile  Dump logs to logfile.\n");
	fprintf(stderr, "-p pidfile  Store process pid in pidfile.\n");
	fprintf(stderr, "-n sock     Path to lldp socket.\n");
	exit(1);
}

static void
netlink_cb(const char *ifname, state_t new_state, void *arg)
{
	struct leaf_info *lti = arg;
	fprintf(stderr, "debug: netlink_cb(upstream=%s blah=%s state=%d)\n",
		lti->upstream_iface, ifname, new_state);
	if (strcmp(lti->upstream_iface, ifname) == 0) {
		lti->upstream_state = new_state;
		if (new_state != LINK_UP) {
			fprintf(stderr, "info: upstream iface %s came down, "
					"killing spree\n",
				ifname);

			leaf_netlink_ifaces_set(lti->ln_control,
						lti->leaf_ifaces_n,
						lti->leaf_ifaces, LINK_DOWN);
		} else if (new_state == LINK_UP) {
			fprintf(stderr, "info: upstream iface %s came up "
					"waiting for lldp to put leaf back "
					"up\n",
				ifname);
		}
	}
}

static void
lldpd_cb(__attribute__((unused)) struct leaf_lldp *ll, const char *ifname,
	 __attribute__((unused)) const char *remote_ifname,
	 __attribute__((unused)) const char *remote_fqdn, state_t new_state,
	 void *data)
{
	struct leaf_info *lti = (struct leaf_info *)data;

	if (strcmp(lti->upstream_iface, ifname) == 0) {
		if (new_state == LINK_UP) {
			fprintf(stderr,
				"info: upstream iface %s came back in lldp, "
				"putting leafs back up\n",
				ifname);

			leaf_netlink_ifaces_set(lti->ln_control,
						lti->leaf_ifaces_n,
						lti->leaf_ifaces, LINK_UP);
		}
	}
}

static inline int
loop(struct leaf_info *lti)
{
	int netlink_fd, lldp_fd, s = 0;
	struct pollfd fds[2];

	netlink_fd = leaf_netlink_fd(lti->ln_watch);
	lldp_fd = leaf_lldp_fd(lti->ll);

	fds[0].fd = netlink_fd;
	fds[0].events = POLLIN;
	fds[1].fd = lldp_fd;
	fds[1].events = POLLIN;

	s = poll(fds, 2, -1);
	if (s < 0 && signal_received > 0)
		return -1;

	switch (fds[0].revents) {
	case POLLIN:
		s = leaf_netlink_recv(lti->ln_watch);
		if (s < 0)
			return s;
	}

	switch (fds[1].revents) {
	case POLLIN:
		s = leaf_lldp_recv(lti->ll);
		if (s < 0)
			return s;
	case POLLHUP:
		leaf_lldp_close_fd(lti->ll);
	case POLLERR:
	case POLLNVAL:
		leaf_lldp_mark_closed_fd(lti->ll);
		break;
	}
	return 0;
}

int
main(int argc, char *argv[])
{
	int debug = 1, ch, foreground = 0, s;
	char *pidfile = NULL, *logfile = NULL, *lldp_name = NULL;
	const char *options = "dfhu:p:l:n:";
	struct leaf_info lti;
	struct pidfh *pidfile_fp = NULL;
	struct sigaction action;

	/* Handle process termination */
	action.sa_handler = termination_handler;
	sigemptyset(&action.sa_mask);
	action.sa_flags = 0;

	signal_received = 0;

	sigaction(SIGINT, &action, NULL);
	sigaction(SIGTERM, &action, NULL);

	/* Parse arguments */
	lti.upstream_iface = NULL;
	while ((ch = getopt(argc, argv, options)) != -1) {
		switch (ch) {
		case 'd':
			debug++;
			break;
		case 'f':
			foreground = 1;
			break;
		case 'u':
			lti.upstream_iface = optarg;
			break;
		case 'l':
			logfile = optarg;
			break;
		case 'p':
			pidfile = optarg;
			break;
		case 'n':
			lldp_name = optarg;
			break;
		case 'h':
		default:
			usage();
		}
	}

	if (lti.upstream_iface == NULL) {
		fprintf(stderr, "err: missing upstream iface definition\n");
		return EXIT_FAILURE;
	}

	/* Open pidfile before fork and chdir */
	if (pidfile != NULL) {
		int process_exists;
		pid_t otherpid;
		pidfile_fp = pidfile_open(pidfile, 0600, &otherpid);

		process_exists = kill(otherpid, 0);
		if (process_exists == 0) {
			fprintf(stderr, "fatal: pidfile points to a "
					"running process\n");
			return EXIT_FAILURE;
		}
	}

	/* stderr is used for logfile */
	if (logfile != NULL) {
		int fd;
		if ((fd = open("/dev/null", O_RDWR)) == -1) {
			perror("failed to open /dev/null");
			return 1;
		}
		dup2(fd, STDIN_FILENO);
		dup2(fd, STDOUT_FILENO);
		if (fd > 2)
			close(fd);
		if ((fd = open(logfile, O_WRONLY | O_APPEND | O_CREAT,
			       S_IRWXU)) == -1) {
			perror("failed to open logfile while daemonising");
			return 1;
		}
		dup2(fd, STDERR_FILENO);
		if (fd > 2)
			close(fd);
	}

	if (foreground == 0) {
		if (daemon(0, 1))
			return EXIT_FAILURE;
		umask(0);
	}

	if (pidfile_fp != NULL) {
		int err;
		err = pidfile_write(pidfile_fp);
		err += pidfile_close(pidfile_fp);
		if (err < 0)
			return EXIT_FAILURE;
	}

	if (leaf_netlink_create(&lti.ln_control, NULL, NULL) < 0) {
		fprintf(stderr, "fatal: creation of netlink context failed\n");
		return EXIT_FAILURE_INIT;
	}

	if (leaf_netlink_create(&lti.ln_watch, netlink_cb, &lti) < 0) {
		fprintf(stderr, "fatal: creation of netlink context failed\n");
		goto destroy_netlink_control;
	}

	if (leaf_netlink_ifaces_exists(
		lti.ln_control, (char *const *)&lti.upstream_iface, 1) < 0) {
		fprintf(stderr, "fatal: upstream iface is not valid\n");
		goto destroy_netlink_control;
	}

	if (leaf_netlink_ifaces_exists(lti.ln_control, argv + optind,
				       argc - optind) < 0) {
		fprintf(stderr, "fatal: leaf ifaces are invalid\n");
		goto destroy_netlink_control;
	}

	lti.leaf_ifaces = argv + optind;
	lti.leaf_ifaces_n = argc - optind;

	if ((s = leaf_lldp_create(lldp_name, &lti.ll, lldpd_cb, &lti)) < 0) {
		fprintf(stderr, "fatal: creation of lldp context failed(%d)\n",
			s);
		goto destroy_netlink_watch;
	}

	s = 1;
	while (s >= 0 && signal_received == 0) {
		s = loop(&lti);
	}

	leaf_lldp_free(lti.ll);
	leaf_netlink_free(lti.ln_watch);
	leaf_netlink_free(lti.ln_control);

	return EXIT_SUCCESS;
destroy_netlink_watch:
	leaf_netlink_free(lti.ln_watch);
destroy_netlink_control:
	leaf_netlink_free(lti.ln_control);
	return EXIT_FAILURE;
}
