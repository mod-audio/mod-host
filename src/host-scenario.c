/*
 * This file is part of mod-host.
 *
 * mod-host is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * mod-host is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with mod-host.  If not, see <http://www.gnu.org/licenses/>.
 *
 */

/*
************************************************************************************************************************
*
************************************************************************************************************************
*/

/*
************************************************************************************************************************
*           INCLUDE FILES
************************************************************************************************************************
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include "host-scenario.h"
#include "host-dispatch.h"
#include "host-errors.h"
#include "protocol.h"
#include "socket.h"
#include "utils.h"


/*
************************************************************************************************************************
*           LOCAL FUNCTIONS
************************************************************************************************************************
*/

static char *trim(char *s)
{
    char *end;

    while (*s == ' ' || *s == '\t')
        s++;
    end = s + strlen(s);
    while (end > s && (end[-1] == ' ' || end[-1] == '\t' || end[-1] == '\n' || end[-1] == '\r'))
        *--end = '\0';
    return s;
}

/* Replaces every $NAME of 'vars' in 'text'. An unknown $NAME is left as it is. */
static char *substitute(const char *text, const host_scenario_var_t *vars, int var_count)
{
    size_t cap = strlen(text) + 1;
    size_t len = 0;
    char *out;
    int i;

    for (i = 0; i < var_count; i++)
        cap += strlen(vars[i].value) * 4;
    out = malloc(cap);
    if (!out)
        return NULL;

    while (*text)
    {
        if (*text == '$')
        {
            for (i = 0; i < var_count; i++)
            {
                size_t n = strlen(vars[i].name);
                if (strncmp(text + 1, vars[i].name, n) == 0)
                {
                    size_t v = strlen(vars[i].value);
                    if (len + v >= cap)
                    {
                        cap = (len + v) * 2;
                        out = realloc(out, cap);
                        if (!out)
                            return NULL;
                    }
                    memcpy(out + len, vars[i].value, v);
                    len += v;
                    text += 1 + n;
                    break;
                }
            }
            if (i < var_count)
                continue;
        }
        if (len + 1 >= cap)
        {
            cap *= 2;
            out = realloc(out, cap);
            if (!out)
                return NULL;
        }
        out[len++] = *text++;
    }
    out[len] = '\0';
    return out;
}

static const char *verb_of(const char *command, char *verb, size_t size)
{
    size_t n = strcspn(command, " ");

    if (n >= size)
        n = size - 1;
    memcpy(verb, command, n);
    verb[n] = '\0';
    return verb;
}

static int reply_accepted(const char *alternatives, const char *reply)
{
    const char *p = alternatives;

    while (*p)
    {
        const char *bar = strchr(p, '|');
        size_t n = bar ? (size_t)(bar - p) : strlen(p);
        while (n > 0 && p[n - 1] == ' ')
            n--;
        while (n > 0 && *p == ' ')
            p++, n--;
        if (strlen(reply) == n && strncmp(p, reply, n) == 0)
            return 1;
        if (!bar)
            break;
        p = bar + 1;
    }
    return 0;
}

/* The table's entry for a verb of the ten, or NULL for a verb outside the table. */
static int table_serves(const host_backend_t *table, const char *verb, int *in_table)
{
    *in_table = 1;
    if (strcmp(verb, "add") == 0) return table->add != NULL;
    if (strcmp(verb, "remove") == 0) return table->remove != NULL;
    if (strcmp(verb, "bypass") == 0) return table->bypass != NULL;
    if (strcmp(verb, "param_set") == 0) return table->param_set != NULL;
    if (strcmp(verb, "param_get") == 0) return table->param_get != NULL;
    if (strcmp(verb, "preset_load") == 0) return table->preset_load != NULL;
    if (strcmp(verb, "state_save") == 0) return table->state_save != NULL;
    if (strcmp(verb, "state_load") == 0) return table->state_load != NULL;
    if (strcmp(verb, "connect") == 0) return table->connect != NULL;
    if (strcmp(verb, "disconnect") == 0) return table->disconnect != NULL;
    *in_table = 0;
    return 0;
}

static int read_reply(int fd, char *buf, size_t size)
{
    size_t got = 0;

    while (got < size - 1)
    {
        ssize_t n = recv(fd, buf + got, 1, 0);
        if (n <= 0)
            return -1;
        if (buf[got] == '\0')
            return 0;
        got++;
    }
    buf[got] = '\0';
    return -1;
}


/*
************************************************************************************************************************
*           GLOBAL FUNCTIONS
************************************************************************************************************************
*/

int host_scenario_load(const char *path, const host_scenario_var_t *vars, int var_count, host_scenario_list_t *list)
{
    FILE *fp = fopen(path, "r");
    char line[1024];

    memset(list, 0, sizeof(*list));
    if (!fp)
        return -1;

    while (fgets(line, sizeof(line), fp))
    {
        char *text = trim(line);
        char *arrow, *guard = NULL;
        host_scenario_t *sc;

        if (*text == '\0' || *text == '#')
            continue;
        arrow = strstr(text, "=>");
        if (!arrow)
        {
            fclose(fp);
            host_scenario_free(list);
            return -1;
        }
        *arrow = '\0';
        if (list->count >= HOST_SCENARIO_MAX_LINES)
        {
            fclose(fp);
            host_scenario_free(list);
            return -1;
        }
        if (*text == '@')
        {
            char *space = strchr(text, ' ');
            if (!space)
            {
                fclose(fp);
                host_scenario_free(list);
                return -1;
            }
            *space = '\0';
            guard = text + 1;
            text = space + 1;
        }
        sc = &list->lines[list->count++];
        sc->command = substitute(trim(text), vars, var_count);
        sc->reply = substitute(trim(arrow + 2), vars, var_count);
        sc->guard = guard ? strdup(guard) : NULL;
    }
    fclose(fp);
    return list->count;
}

void host_scenario_free(host_scenario_list_t *list)
{
    int i;

    for (i = 0; i < list->count; i++)
    {
        free(list->lines[i].command);
        free(list->lines[i].reply);
        free(list->lines[i].guard);
    }
    list->count = 0;
}

int host_scenario_run(const host_scenario_list_t *list, host_scenario_exchange_t exchange, void *ctx,
                      const host_backend_t *table)
{
    char absent[HOST_SCENARIO_MAX_ABSENT][32];
    int absent_count = 0;
    int failures = 0;
    int i, j;

    for (i = 0; i < list->count; i++)
    {
        const host_scenario_t *sc = &list->lines[i];
        char reply[HOST_SCENARIO_REPLY_SIZE];
        char verb[32];

        if (sc->guard)
        {
            for (j = 0; j < absent_count; j++)
            {
                if (strcmp(absent[j], sc->guard) == 0)
                    break;
            }
            if (j < absent_count)
            {
                printf("skip '%s' (%s not served)\n", sc->command, sc->guard);
                continue;
            }
        }

        if (exchange(ctx, sc->command, reply, sizeof(reply)) != 0)
        {
            printf("FAIL '%s': no reply\n", sc->command);
            return -1;
        }

        verb_of(sc->command, verb, sizeof(verb));
        if (strcmp(reply, "resp -902") == 0 && absent_count < HOST_SCENARIO_MAX_ABSENT)
        {
            for (j = 0; j < absent_count; j++)
            {
                if (strcmp(absent[j], verb) == 0)
                    break;
            }
            if (j == absent_count)
                snprintf(absent[absent_count++], sizeof(absent[0]), "%s", verb);
        }

        if (!reply_accepted(sc->reply, reply))
        {
            printf("FAIL '%s': got '%s', want '%s'\n", sc->command, reply, sc->reply);
            failures++;
            continue;
        }
        if (table)
        {
            int in_table;
            int served = table_serves(table, verb, &in_table);
            int refused = strcmp(reply, "resp -902") == 0;
            if (in_table && served == refused)
            {
                printf("FAIL '%s': the table %s %s but the host answered '%s'\n", sc->command,
                       served ? "serves" : "leaves NULL", verb, reply);
                failures++;
                continue;
            }
        }
        printf("ok   '%s' -> '%s'\n", sc->command, reply);
    }
    return failures;
}

int host_scenario_socket_open(const char *host, int port)
{
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in addr;

    if (fd < 0)
        return -1;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    if (inet_pton(AF_INET, host, &addr.sin_addr) != 1)
    {
        close(fd);
        return -1;
    }
    if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0)
    {
        close(fd);
        return -1;
    }
    return fd;
}

int host_scenario_socket_exchange(void *ctx, const char *command, char *reply, size_t size)
{
    int fd = *(int *)ctx;

    if (send(fd, command, strlen(command) + 1, 0) < 0)
        return -1;
    return read_reply(fd, reply, size);
}

int host_scenario_direct_open(host_scenario_direct_t *direct, const host_backend_t *backend)
{
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, direct->fd) < 0)
        return -1;
    direct->backend = backend;
    host_dispatch_register(backend);
    host_dispatch_register_unsupported();
    return 0;
}

int host_scenario_direct_exchange(void *ctx, const char *command, char *reply, size_t size)
{
    host_scenario_direct_t *direct = ctx;
    char *copy = strdup(command);
    msg_t msg;

    if (!copy)
        return -1;
    msg.sender_id = direct->fd[0];
    msg.data = copy;
    msg.data_size = strlen(copy);
    protocol_parse(&msg);
    free(copy);
    return read_reply(direct->fd[1], reply, size);
}

void host_scenario_direct_close(host_scenario_direct_t *direct)
{
    protocol_remove_commands();
    close(direct->fd[0]);
    close(direct->fd[1]);
    direct->fd[0] = direct->fd[1] = -1;
}
