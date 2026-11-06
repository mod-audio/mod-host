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
#include <sys/wait.h>

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

/* a child holds both ports with socket_start(): with `serve` it also closes an accepted connection first, so the port
 * is left in TIME_WAIT on the server's side when the child ends. Answers the child's pid; it ends when told on `go` */
static pid_t holder(int port, int serve, int go[2], int ready[2])
{
    pid_t pid = fork();

    if (pid == 0)
    {
        char c = 0;

        if (socket_start(port, port + 1, 1024) != 0)
            _exit(2);
        if (serve)
        {
            int client = socket(AF_INET, SOCK_STREAM, 0), served;
            struct sockaddr_in addr;

            memset(&addr, 0, sizeof(addr));
            addr.sin_family = AF_INET;
            addr.sin_port = htons(port);
            addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
            if (connect(client, (struct sockaddr *)&addr, sizeof(addr)) != 0 || (served = accept(g_serverfd, NULL, NULL)) < 0)
                _exit(3);
            close(served);
            usleep(100000);
            close(client);
        }
        if (write(ready[1], &c, 1) != 1 || read(go[0], &c, 1) != 1)
            _exit(4);
        _exit(0);
    }
    return pid;
}

/* one port, one daemon: a port a running daemon listens on is refused to a second socket_start (SO_REUSEPORT would
 * let both listen, and the kernel would split the controllers between them), and a port the daemon before left in
 * TIME_WAIT is taken again at once (SO_REUSEADDR) */
static void one_listener(void)
{
    const int port = free_ports();
    int go[2], ready[2], status;
    char c = 0;
    pid_t pid;

    if (pipe(go) != 0 || pipe(ready) != 0)
        exit(EXIT_FAILURE);
    pid = holder(port, 0, go, ready);
    if (read(ready[0], &c, 1) != 1)
    {
        printf("FAILED  a live daemon: the holder did not start\n");
        g_failures++;
    }
    else if (socket_start(port, port + 1, 1024) == 0)
    {
        printf("FAILED  a second daemon was given the ports %d and %d a running one listens on\n", port, port + 1);
        g_failures++;
        socket_finish();
    }
    else if (socket_start(port, 0, 1024) == 0)
    {
        /* the command port alone, with no feedback port: shared, it is the same fault */
        printf("FAILED  a second daemon was given the command port %d a running one listens on\n", port);
        g_failures++;
        socket_finish();
    }
    else
        printf("ok      a second daemon is refused the ports a running one listens on, the command port alone too\n");
    if (write(go[1], &c, 1) != 1)
        exit(EXIT_FAILURE);
    waitpid(pid, &status, 0);

    pid = holder(port, 1, go, ready);
    if (read(ready[0], &c, 1) != 1 || write(go[1], &c, 1) != 1)
    {
        printf("FAILED  a port in TIME_WAIT: the holder did not serve\n");
        g_failures++;
        return;
    }
    waitpid(pid, &status, 0);
    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0)
    {
        printf("FAILED  a port in TIME_WAIT: the holder ended %d\n", status);
        g_failures++;
    }
    else if (socket_start(port, port + 1, 1024) != 0)
    {
        printf("FAILED  the ports a daemon that ended left in TIME_WAIT are not taken again\n");
        g_failures++;
    }
    else
    {
        printf("ok      the ports a daemon that ended left in TIME_WAIT are taken again\n");
        socket_finish();
    }
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
    one_listener();

    if (g_failures)
        printf("%d failed\n", g_failures);
    else
        printf("all passed\n");
    return g_failures ? EXIT_FAILURE : EXIT_SUCCESS;
}
