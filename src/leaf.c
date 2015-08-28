
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <semaphore.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/errno.h>
#include <sys/param.h>
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

struct leaf_thread_info {
	const char *upstream_iface;
	state_t upstream_state;
	struct leaf_netlink *ln_control;
	struct leaf_netlink *ln_watch;
	struct leaf_lldp *ll;
	int leaf_ifaces_n;
	char **leaf_ifaces;
};

uint8_t signal_received;

static void
term_signal(__attribute__((unused)) int sig)
{
	signal_received++;
}

static void
usage()
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
	struct leaf_thread_info *lti = arg;
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
	struct leaf_thread_info *lti = (struct leaf_thread_info *)data;

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
loop(const struct leaf_thread_info *lti)
{
	int netlink_fd, lldp_fd, s = 0;
	fd_set readset;

	netlink_fd = leaf_netlink_fd(lti->ln_watch);
	lldp_fd = leaf_lldp_fd(lti->ll);

	do {
		FD_ZERO(&readset);
		FD_SET(netlink_fd, &readset);
		FD_SET(lldp_fd, &readset);

		s = select(MAX(netlink_fd, lldp_fd) + 1, &readset, NULL, NULL,
			   NULL);
	} while (s == -1 && errno == EINTR);

	if (s > 0) {
		if (FD_ISSET(netlink_fd, &readset)) {
			s = leaf_netlink_recv(lti->ln_watch);
			if (s < 0)
				return s;
		}

		if (FD_ISSET(lldp_fd, &readset)) {
			s = leaf_lldp_recv(lti->ll);
			if (s < 0)
				return s;
		}

		return 0;
	} else {
		perror("loop: error select():");
	}

	return s;
}

int
main(const int argc, char *argv[])
{
	int debug = 1, ch, foreground = 0, s;
	FILE *pidfile_fp = NULL;
	char *pidfile = NULL, *logfile = NULL, *lldp_name = NULL;
	const char *options = "dfhu:p:l:n:";
	struct leaf_thread_info lti;

	signal_received = 0;

	signal(SIGINT, term_signal);
	signal(SIGTERM, term_signal);

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

	if (pidfile != NULL) {
		if (access(pidfile, F_OK) != -1) {
			char buf[16];
			int f, process_exists;
			ssize_t s;
			char *endptr = NULL;
			long int pid;
			f = open(pidfile, O_RDONLY);
			if (f < 0) {
				fprintf(stderr,
					"fatal: error while opening pidfile");
				return 1;
			}
			s = read(f, buf, sizeof(buf) - 1);
			if (s == 0) {
				fprintf(stderr,
					"fatal: existing pidfile is empty");
				return 1;
			}
			if (s == -1) {
				fprintf(stderr,
					"fatal: error while reading pidfile");
				return 1;
			}
			buf[s + 1] = '\0';
			pid = strtol(buf, &endptr, 10);
			if (endptr != NULL && buf == endptr) {
				fprintf(stderr,
					"fatal: pidfile content is empty\n");
				return 1;
			}
			if (endptr != NULL &&
			    !(endptr[0] == '\0' || endptr[0] == '\n')) {
				fprintf(stderr,
					"fatal: pidfile content is invalid\n");
				return 1;
			}

			process_exists = kill(pid, 0);
			if (process_exists == 0) {
				fprintf(stderr, "fatal: pidfile points to a "
						"running process\n");
				return 1;
			} else {
				fprintf(stderr, "info: pidfile points to a "
						"non-existing process, "
						"removing.\n");
				unlink(pidfile);
			}
		}
	}

	if (logfile != NULL) {
		close(STDIN_FILENO);
		if (open("/dev/null", O_RDONLY) == -1) {
			perror("failed to reopen stdin while daemonising");
			return 1;
		}
		close(STDOUT_FILENO);
		if (open("/dev/null", O_WRONLY) == -1) {
			perror("failed to reopen stdout while daemonising");
			return 1;
		}
		close(STDERR_FILENO);
		if (open(logfile, O_WRONLY | O_APPEND | O_CREAT, S_IRWXU) ==
		    -1) {
			perror("failed to reopen stderr while daemonising");
			return 1;
		}
	}

	if (foreground == 0) {
		pid_t pid = fork();
		if (pid == -1) {
			perror("failed to fork while daemonising");
			return 1;
		} else if (pid != 0) {
			return 0;
		}

		if (setsid() == -1) {
			perror("failed to become a session leader while "
			       "daemonising");
			return 1;
		}

		signal(SIGHUP, SIG_IGN);
		pid = fork();
		if (pid == -1) {
			perror("failed to fork while daemonising");
		} else if (pid != 0) {
			return 0;
		}

		umask(0);
	}

	if (pidfile != NULL) {
		pidfile_fp = fopen(pidfile, "w");
		if (pidfile_fp == NULL) {
			fprintf(stderr, "fatal: pidfile creation failed");
			return 1;
		}
		if (pidfile_fp != NULL) {
			pid_t pid;
			pid = getpid();
			fprintf(pidfile_fp, "%d", pid);
			fclose(pidfile_fp);
		}
	}

	if (pidfile != NULL) {
		if (chdir("/") == -1) {
			return 1;
		}
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

	if (leaf_lldp_create(lldp_name, &lti.ll, lldpd_cb, &lti) < 0) {
		fprintf(stderr, "fatal: creation of lldp context failed\n");
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
