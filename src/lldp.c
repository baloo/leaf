#include <bsd/string.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#include <lldpctl.h>

#include "lldp.h"

struct leaf_lldp {
	lldpctl_conn_t *lldp;
	leaf_lldp_callback cb;
	void *data;
	const char *sock_path;
	int fd;
};

static int
lldpd_connect(const char *sock_path)
{
	int s /*, flags*/;
	struct sockaddr_un su;

	if ((s = socket(PF_UNIX, SOCK_STREAM, 0)) == -1)
		return -1;
	su.sun_family = AF_UNIX;
	strlcpy(su.sun_path, sock_path, sizeof(su.sun_path));
	if (connect(s, (struct sockaddr *)&su, sizeof(struct sockaddr_un)) ==
	    -1) {
		fprintf(stderr, "warn: unable to connect to socket %s\n",
			sock_path);
		close(s);
		return -1;
	}

	return s;
}

static ssize_t
lldpd_recv(__attribute__((unused)) lldpctl_conn_t *conn, const uint8_t *data,
	   size_t length, void *user_data)
{
	struct leaf_lldp *ll = (struct leaf_lldp *)user_data;
	ssize_t nb;

	if (ll == NULL)
		return LLDPCTL_ERR_CANNOT_CONNECT;

	if ((nb = read(ll->fd, (uint8_t *)data, length)) == -1) {
		if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)
			return LLDPCTL_ERR_WOULDBLOCK;
		return LLDPCTL_ERR_EOF;
	}

	return nb;
}

static ssize_t
lldpd_send(__attribute__((unused)) lldpctl_conn_t *conn, const uint8_t *data,
	   size_t length, void *user_data)
{
	struct leaf_lldp *ll = (struct leaf_lldp *)user_data;
	ssize_t nb;

	if (ll == NULL)
		return LLDPCTL_ERR_CANNOT_CONNECT;

	if ((nb = write(ll->fd, data, length)) == -1) {
		return LLDPCTL_ERR_CALLBACK_FAILURE;
	}

	return nb;
}

int
leaf_lldp_fd(struct leaf_lldp *ll)
{
	if (ll != NULL) {
		if (ll->fd == -1) {
			return lldpd_connect(ll->sock_path);
		} else {
			return ll->fd;
		}
	}

	return -1;
}

void
leaf_lldp_close_fd(struct leaf_lldp *ll)
{
	if (ll != NULL) {
		if (ll->fd == -1) {
			close(ll->fd);
			ll->fd = -1;
		}
	}
}

void
leaf_lldp_mark_closed_fd(struct leaf_lldp *ll)
{
	if (ll != NULL) {
		ll->fd = -1;
	}
}

int
leaf_lldp_recv(struct leaf_lldp *ll)
{
	int s;
	uint8_t buf[4096];

	s = read(ll->fd, &buf, 4096);
	if (s < 0) {
		if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)
			return 0;
		return -1;
	}
	s = lldpctl_recv(ll->lldp, buf, s);
	if (s < 0)
		return -1;

	return 0;
}

static void
watch_callback(__attribute__((unused)) lldpctl_conn_t *conn,
	       __attribute__((unused)) lldpctl_change_t type,
	       lldpctl_atom_t *iface, lldpctl_atom_t *neighbor, void *data)
{
	struct leaf_lldp *ll = (struct leaf_lldp *)data;
	const char *local_iface_name, *remote_iface_name, *remote_fqdn;
	state_t state = LINK_UNKNOWN;

	local_iface_name =
	    lldpctl_atom_get_str(iface, lldpctl_k_interface_name);
	remote_iface_name =
	    lldpctl_atom_get_str(neighbor, lldpctl_k_interface_name);
	remote_fqdn = lldpctl_atom_get_str(neighbor, lldpctl_k_chassis_name);

	switch (type) {
	case lldpctl_c_deleted:
		state = LINK_DOWN;
		break;
	case lldpctl_c_updated:
	case lldpctl_c_added:
		state = LINK_UP;
		break;
	}

	if (ll->cb != NULL)
		(ll->cb)(ll, local_iface_name, remote_iface_name, remote_fqdn,
			 state, ll->data);
}

int
leaf_lldp_create(const char *ctlname, struct leaf_lldp **ll_p,
		 leaf_lldp_callback cb, void *data)
{
	int err;
	struct leaf_lldp *ll;

	ll = calloc(1, sizeof(struct leaf_lldp));
	if (ll == NULL) {
		return -1;
	}

	ll->fd = -1;
	ll->cb = cb;
	ll->data = data;

	if (ctlname)
		ll->sock_path = ctlname;
	else
		ll->sock_path = lldpctl_get_default_transport();

	ll->lldp = lldpctl_new_name(ll->sock_path, lldpd_send, lldpd_recv, ll);
	if (ll->lldp == NULL) {
		err = -1;
		goto fail;
	}

	if ((ll->fd = lldpd_connect(ll->sock_path)) == -1) {
		err = -2;
		goto fail;
	}

	if (lldpctl_watch_callback(ll->lldp, watch_callback, ll) < 0) {
		/* FIXME log warn "unable to watch for neighbors. %s"
		 * lldpctl_last_strerror(ll->lldp) */
		err = -3;
		goto fail;
	}

	*ll_p = ll;
	return 0;

fail:
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
	}

	free(ll);
}
