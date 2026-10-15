/*
 * Checks how socket_run() splits what it receives into commands.
 *
 * Every command ends with a NUL byte. The receive callback answers each command with its length, so the client
 * can tell how many commands the host saw and where it split them, whatever way the bytes arrived.
 *
 * Build and run with "make socket-run" in this directory (AddressSanitizer reports leaks at exit).
 */

#include "../src/socket.c"

#include <arpa/inet.h>
#include <poll.h>
#include <pthread.h>
#include <signal.h>

void effects_idle_external_uis(void)
{
}

static volatile int g_running = 1;
static int g_port;
static int g_failures;

static void answer_with_length(msg_t *msg)
{
    char answer[32];
    snprintf(answer, sizeof(answer), "%zu", strlen(msg->data));
    socket_send(msg->sender_id, answer, strlen(answer) + 1);
}

static void *server_thread(void *arg)
{
    (void)arg;
    while (g_running)
        socket_run(0);
    return NULL;
}

static int client_connect(void)
{
    struct sockaddr_in addr;
    int fd = socket(AF_INET, SOCK_STREAM, 0);

    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = htons(g_port);

    if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) != 0)
    {
        perror("connect");
        exit(EXIT_FAILURE);
    }

    return fd;
}

/* reads answers for up to one second, or until the host closes the connection; returns them joined with spaces */
static void client_answers(int fd, char *out, size_t out_size)
{
    char buf[256];
    size_t len = 0;
    struct pollfd pfd = { fd, POLLIN, 0 };

    out[0] = '\0';

    while (poll(&pfd, 1, 1000) > 0)
    {
        int count = recv(fd, buf, sizeof(buf), 0);
        if (count <= 0)
        {
            snprintf(out + len, out_size - len, "%sclosed", len ? " " : "");
            return;
        }

        for (int i = 0; i < count && len + 2 < out_size; i++)
            out[len++] = buf[i] ? buf[i] : ' ';
        out[len] = '\0';
    }

    if (len > 0 && out[len - 1] == ' ')
        out[len - 1] = '\0';
}

/* sends each chunk as its own write, with a pause between them so they reach the host as separate reads */
static void check(const char *name, const char *expected, int nchunks, const char **chunks, const size_t *sizes)
{
    char got[256];
    int fd = client_connect();

    for (int i = 0; i < nchunks; i++)
    {
        if (i > 0)
            usleep(100 * 1000);
        socket_send(fd, chunks[i], sizes[i]);
    }

    client_answers(fd, got, sizeof(got));
    closesocket(fd);

    if (strcmp(got, expected) == 0)
    {
        printf("ok      %s\n", name);
    }
    else
    {
        printf("FAILED  %s: expected answers \"%s\", got \"%s\"\n", name, expected, got);
        g_failures++;
    }

    /* let socket_run() see the disconnection and wait for the next client */
    usleep(100 * 1000);
}

/* a command of the given total size, NUL included: "cpu_load" padded with trailing spaces */
static char *command_of_size(size_t size)
{
    char *cmd = malloc(size);
    memset(cmd, ' ', size - 1);
    memcpy(cmd, "cpu_load", 8);
    cmd[size - 1] = '\0';
    return cmd;
}

int main(void)
{
    pthread_t thread;
    struct sockaddr_in addr;
    socklen_t addr_len = sizeof(addr);
    char expected[32];

    /* line by line, so the results are out before a sanitizer report ends the process */
    setvbuf(stdout, NULL, _IOLBF, 0);
    signal(SIGPIPE, SIG_IGN);

    /* port 0: the system picks a free one */
    if (socket_start(0, 0, 1024) != 0)
        return EXIT_FAILURE;
    getsockname(g_serverfd, (struct sockaddr *)&addr, &addr_len);
    g_port = ntohs(addr.sin_port);

    socket_set_receive_cb(answer_with_length);
    pthread_create(&thread, NULL, server_thread, NULL);

    {
        const char *c[] = { "cpu_load" };
        const size_t n[] = { 9 };
        check("one command", "8", 1, c, n);
    }

    {
        /* exactly the receive buffer size, the case that waited for more data before */
        char *cmd = command_of_size(1024);
        const char *c[] = { cmd };
        const size_t n[] = { 1024 };
        check("command of exactly 1024 bytes", "1023", 1, c, n);
        free(cmd);
    }

    {
        char *cmd = command_of_size(2048);
        const char *c[] = { cmd };
        const size_t n[] = { 2048 };
        check("command of exactly 2048 bytes", "2047", 1, c, n);
        free(cmd);
    }

    {
        char *cmd = command_of_size(200000);
        const char *c[] = { cmd };
        const size_t n[] = { 200000 };
        snprintf(expected, sizeof(expected), "%d", 200000 - 1);
        check("command of 200000 bytes", expected, 1, c, n);
        free(cmd);
    }

    {
        const char *c[] = { "cpu_load\0bypass 0 1" };
        const size_t n[] = { 20 };
        check("two commands in one write", "8 10", 1, c, n);
    }

    {
        const char *c[] = { "cpu_", "load" };
        const size_t n[] = { 4, 5 };
        check("one command in two writes", "8", 2, c, n);
    }

    {
        const char *c[] = { "cpu_load\0bypa", "ss 0 1" };
        const size_t n[] = { 13, 7 };
        check("a command and a half, then the rest", "8 10", 2, c, n);
    }

    {
        /* 1024 bytes and no terminator, then the client goes away */
        char *cmd = command_of_size(1025);
        const char *c[] = { cmd };
        const size_t n[] = { 1024 };
        check("1024 bytes without a terminator", "", 1, c, n);
        free(cmd);
    }

    {
        const char *c[] = { "cpu_load" };
        const size_t n[] = { 9 };
        check("one command after all that", "8", 1, c, n);
    }

    g_running = 0;
    socket_finish();
    pthread_join(thread, NULL);

    if (g_failures)
    {
        printf("%d check(s) failed\n", g_failures);
        return EXIT_FAILURE;
    }

    printf("all checks passed\n");
    return EXIT_SUCCESS;
}
