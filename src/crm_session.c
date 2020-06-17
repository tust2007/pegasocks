#include "crm_session.h"
#include "crm_log.h"
#include "unistd.h" // close

static void do_socks5_handshake(struct bufferevent *bev, void *ctx)
{
	crm_session_t *session = (crm_session_t *)ctx;
	crm_session_debug(session, "read triggered");
	// Socks5 local
	// Then choose server type
	struct evbuffer *output = bufferevent_get_output(bev);
	struct evbuffer *input = bufferevent_get_input(bev);

	crm_conn_t *conn = session->conn;

	// read from local
	conn->read_bytes =
		evbuffer_remove(input, conn->rbuf, sizeof conn->rbuf);
	crm_session_debug_buffer(session, (unsigned char *)conn->rbuf,
				 conn->read_bytes);

	// socks5 fsm
	crm_socks5_step(&session->fsm_socks5);

	if (session->fsm_socks5.state == ERR) {
		crm_session_error(session, "%s", session->fsm_socks5.err_msg);
		other_session_event_cb(bev, BEV_EVENT_ERROR, ctx);
	}

	if (session->fsm_socks5.state == PROXY) {
		crm_session_debug(session, "TODO: should tls handshake or ws");
	}

	// write back to local socket
	evbuffer_add(output, conn->wbuf, conn->write_bytes);
}

static void other_session_event_cb(struct bufferevent *bev, short events,
				   void *ctx)
{
	crm_session_t *session = (crm_session_t *)ctx;
	if (events & BEV_EVENT_ERROR)
		crm_session_error(session, "Error from bufferevent");
	if (events & (BEV_EVENT_EOF | BEV_EVENT_ERROR)) {
		bufferevent_free(bev);

		crm_session_free(session);
	}
}

crm_session_t *crm_session_new(crm_socket_t fd,
			       crm_local_server_t *local_server)
{
	crm_session_t *ptr = crm_malloc(sizeof(crm_session_t));
	ptr->conn = crm_conn_new(fd);
	ptr->local_server = local_server;

	// init socks5 structure
	ptr->fsm_socks5.rbuf = ptr->conn->rbuf;
	ptr->fsm_socks5.wbuf = ptr->conn->wbuf;
	ptr->fsm_socks5.read_bytes_ptr = &ptr->conn->read_bytes;
	ptr->fsm_socks5.write_bytes_ptr = &ptr->conn->write_bytes;

	return ptr;
}

void crm_session_start(crm_session_t *session)
{
	// new connection, setup a bufferevent for it
	crm_bev_t *bev = crm_bev_socket_new(session, 0);

	// socks5 handshake
	bufferevent_setcb(bev, do_socks5_handshake, NULL,
			  other_session_event_cb, session);
	bufferevent_enable(bev, EV_READ);
}

void crm_session_free(crm_session_t *session)
{
	if (session->conn) {
		crm_conn_free(session->conn);
		// close fd
		close(session->conn->fd);
	}
	crm_free(session);
}
