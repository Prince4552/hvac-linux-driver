/* SPDX-License-Identifier: MIT */
/*
 * modbus_server: hvacd's Modbus TCP server built on libmodbus. Holds the holding-register
 * table (see modbus_regs.h) and serves up to N clients from the daemon's single select() loop:
 * the caller adds the server's sockets to an fd_set, and hands back the readable ones.
 */
#ifndef MODBUS_SERVER_H
#define MODBUS_SERVER_H

#include <modbus.h>
#include <stdint.h>
#include <sys/select.h>

#include "modbus_regs.h"

#define MB_MAX_CLIENTS 8u

typedef struct {
	modbus_t *ctx;
	modbus_mapping_t *map;
	int listen_fd;
	int client_fds[MB_MAX_CLIENTS];
	uint32_t max_clients;
} mb_server_t;

/** Listens on 0.0.0.0:port. Allocates the libmodbus context and table. Returns 0 or -errno. */
int mb_server_open(mb_server_t *s, uint16_t port, uint32_t max_clients);

/** Closes every socket and frees everything mb_server_open() allocated. Safe to call twice. */
void mb_server_close(mb_server_t *s);

/** Adds the listening socket and client sockets to set; raises *max_fd as needed. */
void mb_server_add_fds(const mb_server_t *s, fd_set *set, int *max_fd);

/** Accepts new clients and answers one request per readable client socket. */
void mb_server_handle(mb_server_t *s, const fd_set *readable);

/** The register table (MB_REG_COUNT words), for reading writes and publishing status. */
uint16_t *mb_server_regs(mb_server_t *s);

/** Number of connected clients. */
uint32_t mb_server_client_count(const mb_server_t *s);

#endif /* MODBUS_SERVER_H */
