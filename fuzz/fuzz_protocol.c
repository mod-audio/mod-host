/* The command parser alone: protocol_parse() over a table of commands that has every shape the real one
   has (fixed words, %i/%s/%f wildcards, a trailing "..."), with callbacks that only count what they were
   given. Properties: no crash, no leak, a callback only ever runs with the argument count its pattern
   allows (exactly, or at least the words before the "..." for a variable pattern) and for a command whose first
   word is its pattern's, a command that names a verb of the table runs its callback exactly when its word
   count is in the row's range and is otherwise answered "few arguments" or "many arguments", any other command
   is answered "not found", and every protocol error code has its message. */
#include "fuzz.h"
#include <stdio.h>

static int g_called;

static const struct { const char *pattern, *verb; uint32_t min, max; } g_table[] = {
    { "add %s %i",              "add",        3, 3 },
    { "remove %i",              "remove",     2, 2 },
    { "param_set %i %s %f",     "param_set",  4, 4 },
    { "param_get %i %s",        "param_get",  3, 3 },
    { "connect %s %s",          "connect",    3, 3 },
    { "quit",                   "quit",       1, 1 },
    { "cpu_load",               "cpu_load",   1, 1 },
    { "midi_map %i %s %i %i",   "midi_map",   5, 5 },
    { "patch_set %i %s ...",    "patch_set",  3, UINT32_MAX },
    { "state_save ...",         "state_save", 1, UINT32_MAX },
};

/* one callback per row, so each checks the count its own pattern allows */
static void check(proto_t *proto, unsigned row)
{
    g_called++;
    /* g_table counts the words before a "...", so a variable pattern is called with that many at least */
    uint32_t min = g_table[row].min;
    /* The parser hands a callback only a command with the argument count its pattern allows: fewer is "few
       arguments", more is "many arguments", and neither reaches the callback.
       mutant: src/protocol.c:184 `proto.list_count < (g_commands[i].count - variable_arguments)` -> `0`
       input: extra-14 */
    if (proto->list_count < min || proto->list_count > g_table[row].max)
        abort();
    /* The callback that runs is the one of the command the input names: its first word is the pattern's.
       mutant: src/protocol.c:216 `g_commands[index].callback(&proto);` -> `g_commands[0].callback(&proto);`
       input: extra-9 */
    if (proto->list_count == 0 || strcmp(proto->list[0], g_table[row].verb) != 0)
        abort();
    protocol_response_int((int)proto->list_count, proto);
}

#define CB(n) static void cb##n(proto_t *proto) { check(proto, n); }
CB(0) CB(1) CB(2) CB(3) CB(4) CB(5) CB(6) CB(7) CB(8) CB(9)
static void (*const g_cbs[])(proto_t *) = { cb0, cb1, cb2, cb3, cb4, cb5, cb6, cb7, cb8, cb9 };

int LLVMFuzzerInitialize(int *argc, char ***argv)
{
    (void)argc; (void)argv;
    for (unsigned i = 0; i < sizeof(g_table) / sizeof(g_table[0]); i++)
        protocol_add_command(g_table[i].pattern, g_cbs[i]);
    return 0;
}

/* The words of a message the way the parser counts them when it has no quotes to read: trailing blanks go, a run of
   spaces is one separator, and a message that starts with a space starts with an empty word. Answers the count and
   the length of the first word. */
static unsigned message_words(const char *m, size_t *first)
{
    size_t len = strlen(m);
    unsigned words = 1;
    while (len > 0 && (m[len - 1] == ' ' || m[len - 1] == '\t' || m[len - 1] == '\r' || m[len - 1] == '\n'))
        len--;
    for (size_t i = 0; i < len; i++)
        if (m[i] == ' ')
        {
            words++;
            while (i + 1 < len && m[i + 1] == ' ')
                i++;
        }
    *first = 0;
    while (*first < len && m[*first] != ' ')
        (*first)++;
    return words;
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    if (size > FUZZ_MAX_INPUT)
        return 0;

    g_called = 0;
    fuzz_reply_clear();

    msg_t msg;
    msg.data = fuzz_message(data, size);
    msg.data_size = strlen(msg.data);
    msg.sender_id = FUZZ_SENDER;

    /* what the input must get, from its words and the table. Left to the callbacks' own checks: a message with a
       quote, a backslash, a "%" or a "..." in it. Quotes join and cut words, and the parser takes a word that is a
       pattern's own wildcard or "..." for a match, so it may answer by an earlier row than the verb's. */
    size_t first;
    unsigned words = message_words(msg.data, &first);
    int expect_called = 0;
    char expect_reply[32] = "not found";
    for (unsigned row = 0; row < sizeof(g_table) / sizeof(g_table[0]); row++)
    {
        if (strlen(g_table[row].verb) != first || strncmp(msg.data, g_table[row].verb, first) != 0)
            continue;
        if (words < g_table[row].min)
            strcpy(expect_reply, "few arguments");
        else if (words > g_table[row].max)
            strcpy(expect_reply, "many arguments");
        else
        {
            expect_called = 1;
            snprintf(expect_reply, sizeof(expect_reply), "resp %u", words);
        }
    }

    int exact = strpbrk(msg.data, "%\"\\") == NULL && strstr(msg.data, "...") == NULL;

    protocol_parse(&msg);
    free(msg.data);

    /* A command that names a verb of the table with a word count in its row's range runs its callback once, and any
       other command runs none.
       mutant: src/protocol.c:196 `match == proto.list_count || variable_arguments` -> `0`
       input: extra-1 */
    if (exact && g_called != expect_called)
        abort();
    /* The parser answers once, with its NUL: the callback's "resp <words>", "few arguments" below the row's minimum,
       "many arguments" above its maximum, or "not found" for a verb outside the table.
       mutant: src/protocol.c:186 `index = PROTOCOL_FEW_ARGUMENTS;` -> `index = PROTOCOL_MANY_ARGUMENTS;`
       input: extra-14
       mutant: src/protocol.c:192 `index = PROTOCOL_MANY_ARGUMENTS;` -> `index = PROTOCOL_FEW_ARGUMENTS;`
       input: extra-16 */
    if (exact && (fuzz_reply_len != strlen(expect_reply) + 1 || strcmp(fuzz_reply, expect_reply) != 0))
        abort();

    return 0;
}
