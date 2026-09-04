#ifndef NET_TCP_H
#define NET_TCP_H

#include "protocol.h"

/* mTCP must already be initialized (Utils::parseEnv, Packet_init, Arp::init,
 * Tcp::init -- see net_tcp.cpp) before calling tcp_open. That init happens
 * once in main() at program start, not per-connection.
 *
 * host: dotted-quad IP string (e.g. "192.168.1.50"). mTCP's DNS resolver
 * could be used instead, but a fixed IP for the bridge keeps this simple
 * and avoids a DNS round trip on every run.
 */
int mtcp_stack_init(void);
void mtcp_stack_shutdown(void);

int tcp_open(const char *host, unsigned short port, int timeout_ticks);
void tcp_close(void);
void tcp_get_transport(Transport *t);

#endif
