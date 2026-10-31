/*
 * Checks who reaches the ports socket_start() opens. A port is reached when a connect to it from this machine succeeds:
 * a listening socket takes the connection whether or not socket_run() is accepting yet.
 *
 * By default only 127.0.0.1 reaches them. 127.0.0.2 is also this machine, and a port bound to 127.0.0.1 alone refuses it,
 * so that arm runs on a host with no network; an address off the loopback, when the machine has one, is the real case.
 *
 * Build and run with "make bind-run" in this directory.
 */

#include "../src/socket.c"

#include <errno.h>
#include <ifaddrs.h>
#include <net/if.h>

void effects_idle_external_uis(void)
{
}

static int g_failures;

static int reaches(const char *address, int port)
{
    struct sockaddr_in addr;
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    int rc;

    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    inet_pton(AF_INET, address, &addr.sin_addr);
    rc = connect(fd, (struct sockaddr *)&addr, sizeof(addr));
    if (rc != 0 && errno != ECONNREFUSED)
        printf("note    connect %s:%d: %s\n", address, port, strerror(errno));
    close(fd);
    return rc == 0;
}

static void expect(const char *arm, const char *address, int port, int want)
{
    const int got = reaches(address, port);

    printf("%s  %s: %s:%d %s\n", got == want ? "ok    " : "FAILED", arm, address, port, got ? "reached" : "refused");
    if (got != want)
        g_failures++;
}

/* an up IPv4 address of this machine off the loopback, or NULL */
static const char *off_loopback(void)
{
    static char text[INET_ADDRSTRLEN];
    struct ifaddrs *all, *ifa;
    const char *found = NULL;

    if (getifaddrs(&all) != 0)
        return NULL;
    for (ifa = all; ifa && !found; ifa = ifa->ifa_next)
        if (ifa->ifa_addr && ifa->ifa_addr->sa_family == AF_INET && (ifa->ifa_flags & IFF_UP) && !(ifa->ifa_flags & IFF_LOOPBACK))
            found = inet_ntop(AF_INET, &((struct sockaddr_in *)ifa->ifa_addr)->sin_addr, text, sizeof(text));
    freeifaddrs(all);
    return found;
}

/* two free ports, one after the other */
static int free_ports(void)
{
    for (int port = 20000 + getpid() % 20000; port < 65000; port += 2)
    {
        struct sockaddr_in addr;
        int a = socket(AF_INET, SOCK_STREAM, 0), b = socket(AF_INET, SOCK_STREAM, 0), ok;

        memset(&addr, 0, sizeof(addr));
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = htonl(INADDR_ANY);
        addr.sin_port = htons(port);
        ok = bind(a, (struct sockaddr *)&addr, sizeof(addr)) == 0;
        addr.sin_port = htons(port + 1);
        ok = ok && bind(b, (struct sockaddr *)&addr, sizeof(addr)) == 0;
        close(a);
        close(b);
        if (ok)
            return port;
    }
    printf("FAILED  no free ports\n");
    exit(EXIT_FAILURE);
}

/* both ports of one socket_start: each address in `yes` reaches them, each in `no` does not */
static void arm(const char *name, const char *const *yes, const char *const *no)
{
    const int port = free_ports();

    if (socket_start(port, port + 1, 1024) != 0)
    {
        printf("FAILED  %s: socket_start\n", name);
        g_failures++;
        return;
    }
    for (; *yes; yes++)
    {
        expect(name, *yes, port, 1);
        expect(name, *yes, port + 1, 1);
    }
    for (; *no; no++)
    {
        expect(name, *no, port, 0);
        expect(name, *no, port + 1, 0);
    }
    socket_finish();
}

int main(void)
{
    const char *const nic = off_loopback();
    const char *const bad[] = { "", "localhost", "::1", "127.0.0", "127.0.0.1 ", "256.0.0.1", NULL };

    if (nic)
        printf("off the loopback: %s\n", nic);
    else
        printf("NOT RUN: this machine has no address off the loopback; 127.0.0.2 stands for it\n");

    {
        const char *const yes[] = { "127.0.0.1", NULL };
        const char *const no[] = { "127.0.0.2", nic, NULL };

        arm("by default", yes, no);
    }
    {
        const char *const yes[] = { "127.0.0.1", "127.0.0.2", nic, NULL };
        const char *const no[] = { NULL };

        if (socket_set_bind_address("0.0.0.0") != 0)
            g_failures++;
        arm("0.0.0.0", yes, no);
    }
    if (nic)
    {
        const char *const yes[] = { nic, NULL };
        const char *const no[] = { "127.0.0.1", NULL };

        if (socket_set_bind_address(nic) != 0)
            g_failures++;
        arm("one address", yes, no);
    }
    if (socket_set_bind_address("127.0.0.1") != 0)
        g_failures++;
    if (socket_set_bind_address(NULL) != -1)
    {
        printf("FAILED  NULL taken as an address\n");
        g_failures++;
    }
    for (int i = 0; bad[i]; i++)
        if (socket_set_bind_address(bad[i]) != -1)
        {
            printf("FAILED  '%s' taken as an address\n", bad[i]);
            g_failures++;
        }
    {
        const char *const yes[] = { "127.0.0.1", NULL };
        const char *const no[] = { "127.0.0.2", nic, NULL };

        arm("after refused addresses", yes, no);
    }

    if (g_failures)
        printf("%d failed\n", g_failures);
    else
        printf("all passed\n");
    return g_failures ? EXIT_FAILURE : EXIT_SUCCESS;
}
