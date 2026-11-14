/* Shared by the libFuzzer harnesses of this directory (fuzz/README.md). */
#ifndef FUZZ_H
#define FUZZ_H

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "../src/utils.h"
#include "../src/protocol.h"

#define FUZZ_MAX_INPUT 4096

/* Where protocol_parse() answers: fuzz/socket_fuzz.c replaces src/socket.c in every harness, and its socket_send()
   appends each reply, with its NUL, to fuzz_reply. A harness empties it before an input and reads it after. */
#define FUZZ_REPLY_MAX 65536
extern char fuzz_reply[FUZZ_REPLY_MAX];
extern size_t fuzz_reply_len;

/* the sender id to put in msg_t: any value but STDOUT_FILENO, which the parser writes to directly */
#define FUZZ_SENDER 7

static void fuzz_reply_clear(void)
{
    fuzz_reply_len = 0;
    fuzz_reply[0] = '\0';
}

/* The input as the NUL-terminated message the socket layer hands the parser (it cuts at the first NUL). */
static char *fuzz_message(const uint8_t *data, size_t size)
{
    char *copy = malloc(size + 1);
    /* not a property: the harness's own allocation failed */
    if (!copy)
        abort();
    memcpy(copy, data, size);
    copy[size] = '\0';
    return copy;
}

#endif
