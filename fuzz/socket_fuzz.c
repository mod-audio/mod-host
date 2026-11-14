/* The socket layer of the fuzz harnesses (fuzz/README.md). It stands in for src/socket.c, which the harnesses do not
   link: socket_send() copies the reply into fuzz_reply, where the harness reads it, so a run opens no socket and
   touches no descriptor. The shipped library does not contain this file. */
#include <string.h>

#include "../src/socket.h"
#include "fuzz.h"

char fuzz_reply[FUZZ_REPLY_MAX];
size_t fuzz_reply_len;

int socket_send(int destination, const char *buffer, int size)
{
    (void)destination;
    if (size <= 0)
        return 0;
    /* a reply past the buffer is cut: no reply of the parser or the dispatcher is that long */
    size_t room = FUZZ_REPLY_MAX - fuzz_reply_len;
    size_t n = (size_t)size < room ? (size_t)size : room;
    memcpy(fuzz_reply + fuzz_reply_len, buffer, n);
    fuzz_reply_len += n;
    return size;
}

int socket_send_feedback(const char *buffer)
{
    (void)buffer;
    return -1;
}

int socket_start(int socket_port, int feedback_port, int buffer_size)
{
    (void)socket_port; (void)feedback_port; (void)buffer_size;
    return -1;
}

void socket_finish(void) {}
void socket_set_receive_cb(void (*receive_cb)(msg_t *msg)) { (void)receive_cb; }
void socket_set_idle_cb(void (*idle_cb)(void)) { (void)idle_cb; }
void socket_set_idle_interval(int interval_ms) { (void)interval_ms; }
void socket_run(int exit_on_failure) { (void)exit_on_failure; }
