// SPDX-License-Identifier: MIT
/*
 * modbus_server: Modbus TCP server for hvacd using libmodbus. One context and one register table
 * are allocated at start-up; nothing is allocated while serving. Each readable client gets exactly
 * one request answered per loop pass, with a short timeout so a slow client cannot stall control.
 */
#include "modbus_server.h"

#include <errno.h>
#include <stdlib.h>
#include <unistd.h>

#include "log.h"

#define MB_LISTEN_BACKLOG 8
#define MB_REQUEST_TIMEOUT_US 200000u	/* longest wait for the rest of a started request */

static void close_fd(int *fd)
{
	if (*fd >= 0) {
		(void)close(*fd);	/* nothing useful to do if close fails during cleanup */
		*fd = -1;
	}
}

int mb_server_open(mb_server_t *s, uint16_t port, uint32_t max_clients)
{
	*s = (mb_server_t){ .listen_fd = -1 };
	for (uint32_t i = 0; i < MB_MAX_CLIENTS; i++)
		s->client_fds[i] = -1;
	if (max_clients == 0u || max_clients > MB_MAX_CLIENTS)
		return -EINVAL;
	s->max_clients = max_clients;

	s->ctx = modbus_new_tcp(NULL, (int)port);	/* NULL: listen on every address */
	if (s->ctx == NULL)
		return -ENOMEM;
	if (modbus_set_indication_timeout(s->ctx, 0, MB_REQUEST_TIMEOUT_US) != 0 ||
	    modbus_set_byte_timeout(s->ctx, 0, MB_REQUEST_TIMEOUT_US) != 0) {
		mb_server_close(s);
		return -EINVAL;
	}
	s->map = modbus_mapping_new(0, 0, MB_REG_COUNT, 0);
	if (s->map == NULL) {
		mb_server_close(s);
		return -ENOMEM;
	}
	s->listen_fd = modbus_tcp_listen(s->ctx, MB_LISTEN_BACKLOG);
	if (s->listen_fd < 0) {
		int err = errno != 0 ? -errno : -EIO;

		mb_server_close(s);
		return err;
	}
	return 0;
}

void mb_server_close(mb_server_t *s)
{
	for (uint32_t i = 0; i < MB_MAX_CLIENTS; i++)
		close_fd(&s->client_fds[i]);
	close_fd(&s->listen_fd);
	if (s->map != NULL) {
		modbus_mapping_free(s->map);
		s->map = NULL;
	}
	if (s->ctx != NULL) {
		modbus_free(s->ctx);
		s->ctx = NULL;
	}
}

void mb_server_add_fds(const mb_server_t *s, fd_set *set, int *max_fd)
{
	if (s->listen_fd < 0)
		return;
	FD_SET(s->listen_fd, set);
	if (s->listen_fd > *max_fd)
		*max_fd = s->listen_fd;
	for (uint32_t i = 0; i < s->max_clients; i++) {
		if (s->client_fds[i] < 0)
			continue;
		FD_SET(s->client_fds[i], set);
		if (s->client_fds[i] > *max_fd)
			*max_fd = s->client_fds[i];
	}
}

static void accept_client(mb_server_t *s)
{
	int listen_fd = s->listen_fd;
	int fd = modbus_tcp_accept(s->ctx, &listen_fd);

	if (fd < 0) {
		log_msg(LOG_WARN, "modbus: accept failed: %s", modbus_strerror(errno));
		return;
	}
	for (uint32_t i = 0; i < s->max_clients; i++) {
		if (s->client_fds[i] < 0) {
			s->client_fds[i] = fd;
			log_msg(LOG_DEBUG, "modbus: client connected (slot %u)", i);
			return;
		}
	}
	log_msg(LOG_WARN, "modbus: refusing client, %u already connected", s->max_clients);
	(void)close(fd);
}

/* Answers one request from a client; closes the connection on error or disconnect. */
static void serve_client(mb_server_t *s, uint32_t slot)
{
	uint8_t query[MODBUS_TCP_MAX_ADU_LENGTH] = {0};
	int rc = 0;

	if (modbus_set_socket(s->ctx, s->client_fds[slot]) != 0) {
		close_fd(&s->client_fds[slot]);
		return;
	}
	rc = modbus_receive(s->ctx, query);
	if (rc > 0) {
		if (modbus_reply(s->ctx, query, rc, s->map) < 0)
			log_msg(LOG_DEBUG, "modbus: reply failed: %s", modbus_strerror(errno));
	} else if (rc < 0) {
		log_msg(LOG_DEBUG, "modbus: client in slot %u disconnected", slot);
		close_fd(&s->client_fds[slot]);
	}
	/* rc == 0: request for another unit id, ignored as the protocol requires. */
}

void mb_server_handle(mb_server_t *s, const fd_set *readable)
{
	for (uint32_t i = 0; i < s->max_clients; i++) {
		if (s->client_fds[i] >= 0 && FD_ISSET(s->client_fds[i], readable))
			serve_client(s, i);
	}
	if (s->listen_fd >= 0 && FD_ISSET(s->listen_fd, readable))
		accept_client(s);
}

uint16_t *mb_server_regs(mb_server_t *s)
{
	return s->map->tab_registers;
}

uint32_t mb_server_client_count(const mb_server_t *s)
{
	uint32_t n = 0;

	for (uint32_t i = 0; i < s->max_clients; i++) {
		if (s->client_fds[i] >= 0)
			n++;
	}
	return n;
}
