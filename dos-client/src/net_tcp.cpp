/* ============================================================================
 * !!! VERIFY AGAINST YOUR mTCP SDK BEFORE BUILDING !!!
 *
 * This file was written from memory of mTCP's published sample programs
 * (htget, irc, etc. from Michael Brutman's mTCP SDK, http://www.brutman.com/mTCP/)
 * without the actual SDK headers in front of me -- I could not compile or
 * test this in the environment I was written in (no OpenWatcom/mTCP/DOSBox
 * available there). Treat the mTCP-specific calls below (Utils::parseEnv,
 * Packet_init, Arp::init, Tcp::init, the TcpSocket class and its methods) as
 * "this is the shape of it" rather than "this is guaranteed correct."
 *
 * Before building: download the mTCP SDK, open its sample .cpp files
 * (e.g. APPS/HTGET/HTGET.CPP) side by side with this file, and fix any
 * function/method names or argument orders that don't match your copy.
 * The line-buffering logic below (tcp_write_line/tcp_read_line) and the
 * rest of the codebase do NOT depend on mTCP internals and should not need
 * changes.
 * ============================================================================
 */
#include <stdio.h>
#include <string.h>
#include <dos.h>

#include "types.h"
#include "utils.h"
#include "packet.h"
#include "arp.h"
#include "tcp.h"
#include "tcpsockm.h"

extern "C" {
#include "net_tcp.h"
}

static TcpSocket *s_sock = NULL;
static int s_timeout_ticks = 90;
static char s_linebuf[PROTO_MAX_LINE + 2];

static unsigned long bios_ticks(void)
{
    union REGS r;
    r.h.ah = 0x00;
    int86(0x1A, &r, &r);
    return ((unsigned long) r.x.cx << 16) | r.x.dx;
}

/* Called once at program startup, not per-connection. Reads MTCP.CFG via
 * the environment variable set by the mTCP packet driver / config, brings
 * up the packet driver and ARP. Returns 0 on success. */
int mtcp_stack_init(void)
{
    if (Utils::parseEnv() != 0) {
        fprintf(stderr, "mTCP: environment not set (run appname with -h, "
                         "check MTCPCFG.CFG / packet driver)\n");
        return -1;
    }

    if (Packet_init(Utils::getMyIpAddr(), NULL, NULL) != 0) {
        fprintf(stderr, "mTCP: packet driver init failed\n");
        return -1;
    }

    if (Arp::init() != 0) {
        fprintf(stderr, "mTCP: ARP init failed\n");
        return -1;
    }

    Arp::send(Utils::getGateway());

    if (Tcp::init() != 0) {
        fprintf(stderr, "mTCP: TCP init failed\n");
        return -1;
    }

    return 0;
}

void mtcp_stack_shutdown(void)
{
    Tcp::stop();
    Arp::stop();
    Packet_stop();
}

int tcp_open(const char *host, unsigned short port, int timeout_ticks)
{
    IpAddr_t addr;

    s_timeout_ticks = timeout_ticks;

    if (!Utils::parseIpAddr(host, addr)) {
        fprintf(stderr, "tcp_open: bad IP address '%s'\n", host);
        return -1;
    }

    s_sock = new TcpSocket();
    if (s_sock == NULL)
        return -1;

    if (s_sock->connect(port, addr, port, timeout_ticks * 55) != 0) {
        delete s_sock;
        s_sock = NULL;
        return -1;
    }

    return 0;
}

void tcp_close(void)
{
    if (s_sock) {
        s_sock->close();
        delete s_sock;
        s_sock = NULL;
    }
}

static int tcp_write_line(const char *line)
{
    char out[PROTO_MAX_LINE + 3];
    int len;

    if (!s_sock || !s_sock->isConnected())
        return -1;

    len = sprintf(out, "%s\r\n", line);
    return (s_sock->send((unsigned char *) out, len) == len) ? 0 : -1;
}

static int tcp_read_line(char *buf, int buflen)
{
    int n = 0;
    unsigned long start = bios_ticks();

    if (!s_sock)
        return -1;

    for (;;) {
        unsigned char c;
        int got;

        if (!s_sock->isConnected() && s_sock->recvReadyCount() == 0)
            return -1;

        got = s_sock->recv(&c, 1);

        if (got <= 0) {
            if (bios_ticks() - start > (unsigned long) s_timeout_ticks)
                return -1;
            Tcp::drivePackets();
            Arp::drivePackets();
            continue;
        }

        start = bios_ticks();

        if (c == '\n')
            break;
        if (c == '\r')
            continue;

        if (n < buflen - 1)
            buf[n++] = (char) c;
    }

    buf[n] = '\0';
    return n;
}

void tcp_get_transport(Transport *t)
{
    t->read_line = tcp_read_line;
    t->write_line = tcp_write_line;
}
