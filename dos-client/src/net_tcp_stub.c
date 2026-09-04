/* Stand-in for net_tcp.cpp when building without the mTCP SDK (SERIALONLY=1
 * in the Makefile). Lets you build and test the serial transport path
 * before you've got mTCP set up. TRANSPORT=TCP in HACONFIG.INI will just
 * fail cleanly at connect time. */
#include "net_tcp.h"

int mtcp_stack_init(void) { return -1; }
void mtcp_stack_shutdown(void) {}
int tcp_open(const char *host, unsigned short port, int timeout_ticks)
{
    (void) host; (void) port; (void) timeout_ticks;
    return -1;
}
void tcp_close(void) {}
void tcp_get_transport(Transport *t) { (void) t; }
