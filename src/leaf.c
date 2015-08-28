
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <pthread.h>
#include <string.h>
#include <semaphore.h>
#include <fcntl.h>
#include <signal.h>
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
	sem_t leaf_ifaces_lock;
	int leaf_ifaces_n;
	char **leaf_ifaces;
};

static void
usage()
{
	fprintf(stderr, "Usage:   %s [OPTIONS ...] [LEAF IFACES ...]\n",
		__progname);
	fprintf(stderr, "-d          Enable more debugging information.\n");
	fprintf(stderr, "-u iface    Upstream interface.\n");
	exit(1);
}

static void
netlink_cb(const char *ifname, state_t new_state, void *arg)
{
	struct leaf_thread_info *lti = (struct leaf_thread_info *)arg;
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

static void *
netlink_thread(void *arg)
{
	struct leaf_thread_info *lti = (struct leaf_thread_info *)arg;

	if (leaf_netlink_main(lti->ln_watch, netlink_cb, lti) != 0) {
		leaf_lldp_stop(lti->ll);
		return NULL;
	}

	return NULL;
}

static void *
lldpd_thread(void *arg)
{
	struct leaf_thread_info *lti = (struct leaf_thread_info *)arg;

	return lti;
	return NULL;
}

int
main(const int argc, char *argv[])
{
	int debug = 1, ch, err, foreground = 0;
	FILE *pidfile_fp = NULL;
	char *pidfile = NULL, *logfile = NULL;
	const char *options = "dhu:p:l:";
	pthread_t lldp_thread_id, netlink_thread_id;
	pthread_attr_t attr;
	struct leaf_thread_info lti;

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
			char *endptr;
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
			buf[15] = '\0';
			pid = strtol(buf, &endptr, 10);
			if (endptr != NULL && buf == endptr) {
				fprintf(stderr,
					"fatal: pidfile content is invalid");
				return 1;
			}
			if (endptr != NULL &&
			    (endptr[0] != '\0' || endptr[0] != '\n')) {
				fprintf(stderr,
					"fatal: pidfile content is invalid");
				return 1;
			}

			process_exists = kill(pid, 0);
			if (process_exists == 0) {
				fprintf(stderr, "fatal: pidfile points to a "
						"running process");
				return 1;
			} else {
				fprintf(stderr, "info: pidfile points to a "
						"non-existing process, "
						"removing.");
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
		if (open(logfile, O_WRONLY | O_APPEND | O_CREAT, S_IRWXU) == -1) {
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

	if (pidfile != NULL) {
		if (chdir("/") == -1) {
			return 1;
		}
	}

	if (sem_init(&lti.leaf_ifaces_lock, 0, 1) != 0) {
		perror("fatal: unable to sem_init");
		return EXIT_FAILURE;
	}

	if (leaf_netlink_create(&lti.ln_control) < 0) {
		fprintf(stderr, "fatal: creation of netlink context failed\n");
		return EXIT_FAILURE_INIT;
	}

	if (leaf_netlink_create(&lti.ln_watch) < 0) {
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

	if (leaf_lldp_create(&lti.ll) < 0) {
		fprintf(stderr, "fatal: creation of lldp context failed\n");
		goto destroy_netlink_watch;
	}

	err = pthread_attr_init(&attr);
	if (err != 0) {
		goto destroy_lldp;
	}

	err = pthread_create(&netlink_thread_id, &attr, &netlink_thread, &lti);
	if (err != 0) {
		perror("unable to start netlink_thread");
		goto destroy_lldp;
	}

	err = pthread_create(&lldp_thread_id, &attr, &lldpd_thread, &lti);
	if (err != 0) {
		perror("unable to start lldp_thread");
		goto destroy_lldp;
	}

	err = pthread_attr_destroy(&attr);
	if (err != 0) {
		goto destroy_lldp;
	}

	err = pthread_join(netlink_thread_id, NULL);
	if (err != 0) {
		perror("unable to join netlink_thread");
		goto destroy_lldp;
	}
	err = pthread_join(lldp_thread_id, NULL);
	if (err != 0) {
		perror("unable to join lldp_thread");
		goto destroy_lldp;
	}

	leaf_lldp_free(lti.ll);
	leaf_netlink_free(lti.ln_watch);
	leaf_netlink_free(lti.ln_control);

	return EXIT_SUCCESS;
destroy_lldp:
	leaf_lldp_free(lti.ll);
destroy_netlink_watch:
	leaf_netlink_free(lti.ln_watch);
destroy_netlink_control:
	leaf_netlink_free(lti.ln_control);
	return EXIT_FAILURE;
}
