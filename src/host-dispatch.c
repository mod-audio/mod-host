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

#include "host-dispatch.h"
#include "host-errors.h"
#include "mod-host.h"
#include "socket.h"


/*
************************************************************************************************************************
*           LOCAL GLOBAL VARIABLES
************************************************************************************************************************
*/

static const host_backend_t *g_backend;
static int (*g_monitor_output)(int instance, const char *symbol);
static const host_plugin_info_t *g_plugin_info;


/*
************************************************************************************************************************
*           LOCAL FUNCTIONS
************************************************************************************************************************
*/

static void add_cb(proto_t *proto)
{
    int resp;
    /* proto->list[3] is the optional client-name argument the "..." tail in EFFECT_ADD
       lets through; a caller that sent only 3 tokens leaves it unset. */
    const char *client_name = (proto->list_count > 3) ? proto->list[3] : NULL;
    if (g_backend->add)
        resp = g_backend->add(proto->list[1], atoi(proto->list[2]), client_name);
    else
        resp = ERR_INVALID_OPERATION;
    protocol_response_int(resp, proto);
}

static void remove_cb(proto_t *proto)
{
    int resp;
    if (g_backend->remove)
        resp = g_backend->remove(atoi(proto->list[1]));
    else
        resp = ERR_INVALID_OPERATION;
    protocol_response_int(resp, proto);
}

static void preset_load_cb(proto_t *proto)
{
    int resp;
    if (g_backend->preset_load)
        resp = g_backend->preset_load(atoi(proto->list[1]), proto->list[2]);
    else
        resp = ERR_INVALID_OPERATION;
    protocol_response_int(resp, proto);
}

static void connect_cb(proto_t *proto)
{
    int resp;
    if (g_backend->connect)
        resp = g_backend->connect(proto->list[1], proto->list[2]);
    else
        resp = ERR_INVALID_OPERATION;
    protocol_response_int(resp, proto);
}

static void disconnect_cb(proto_t *proto)
{
    int resp;
    if (g_backend->disconnect)
        resp = g_backend->disconnect(proto->list[1], proto->list[2]);
    else
        resp = ERR_INVALID_OPERATION;
    protocol_response_int(resp, proto);
}

static void bypass_cb(proto_t *proto)
{
    int resp;
    if (g_backend->bypass)
        resp = g_backend->bypass(atoi(proto->list[1]), atoi(proto->list[2]));
    else
        resp = ERR_INVALID_OPERATION;
    protocol_response_int(resp, proto);
}

static void param_set_cb(proto_t *proto)
{
    int resp;
    if (g_backend->param_set)
        resp = g_backend->param_set(atoi(proto->list[1]), proto->list[2], atof(proto->list[3]));
    else
        resp = ERR_INVALID_OPERATION;
    protocol_response_int(resp, proto);
}

static void param_get_cb(proto_t *proto)
{
    int resp;
    float value;
    if (g_backend->param_get)
        resp = g_backend->param_get(atoi(proto->list[1]), proto->list[2], &value);
    else
        resp = ERR_INVALID_OPERATION;

    char buffer[128];
    if (resp >= 0)
        sprintf(buffer, "resp %i %.04f", resp, value);
    else
        sprintf(buffer, "resp %i", resp);

    protocol_response(buffer, proto);
}

static void state_load_cb(proto_t *proto)
{
    int resp;
    if (g_backend->state_load)
        resp = g_backend->state_load(proto->list[1]);
    else
        resp = ERR_INVALID_OPERATION;
    protocol_response_int(resp, proto);
}

static void state_save_cb(proto_t *proto)
{
    int resp;
    if (g_backend->state_save)
        resp = g_backend->state_save(proto->list[1]);
    else
        resp = ERR_INVALID_OPERATION;
    protocol_response_int(resp, proto);
}

/* mod-host answers 1 once the output is monitored and 0 when it cannot be */
static void monitor_output_cb(proto_t *proto)
{
    int resp;
    if (g_monitor_output)
        resp = !g_monitor_output(atoi(proto->list[1]), proto->list[2]);
    else
        resp = ERR_INVALID_OPERATION;
    protocol_response_int(resp, proto);
}

/* a name: valid UTF-8, at most 255 bytes, no control character */
static int valid_name(const char *s)
{
    const unsigned char *p = (const unsigned char *)s;
    size_t len = strlen(s);

    if (len >= HOST_INFO_STRING_SIZE)
        return 0;
    while (*p)
    {
        int extra;
        if (*p < 0x20 || *p == 0x7f)
            return 0;
        if (*p < 0x80)
            extra = 0;
        else if ((*p & 0xe0) == 0xc0 && *p >= 0xc2)
            extra = 1;
        else if ((*p & 0xf0) == 0xe0)
            extra = 2;
        else if ((*p & 0xf8) == 0xf0 && *p <= 0xf4)
            extra = 3;
        else
            return 0;
        for (p++; extra > 0; extra--, p++)
            if ((*p & 0xc0) != 0x80)
                return 0;
    }
    return 1;
}

static int valid_color(const char *s)
{
    int i;

    if (strcmp(s, "-") == 0)
        return 1;
    if (s[0] != '#' || strlen(s) != 7)
        return 0;
    for (i = 1; i < 7; i++)
        if (!strchr("0123456789abcdefABCDEF", s[i]))
            return 0;
    return 1;
}

static void track_info_cb(proto_t *proto)
{
    const char *kind = proto->list_count > 4 ? proto->list[4] : NULL;
    int resp;

    if (proto->list_count > 5 || !valid_name(proto->list[2]) || !valid_color(proto->list[3]) ||
        (kind && strcmp(kind, "bus") != 0 && strcmp(kind, "return") != 0 && strcmp(kind, "master") != 0))
        resp = ERR_INVALID_OPERATION;
    else if (g_plugin_info && g_plugin_info->track_info)
        resp = g_plugin_info->track_info(atoi(proto->list[1]), proto->list[2], proto->list[3], kind);
    else
        resp = ERR_INVALID_OPERATION;
    protocol_response_int(resp, proto);
}

static void remote_pages_cb(proto_t *proto)
{
    int resp;
    if (g_plugin_info && g_plugin_info->remote_pages)
        resp = g_plugin_info->remote_pages(atoi(proto->list[1]));
    else
        resp = ERR_INVALID_OPERATION;
    protocol_response_int(resp, proto);
}

/* appends " \"text\"", a quote inside written as \" */
static size_t append_quoted(char *buf, size_t pos, size_t size, const char *text)
{
    if (pos + 2 < size)
    {
        buf[pos++] = ' ';
        buf[pos++] = '"';
    }
    for (; *text && pos + 3 < size; text++)
    {
        if (*text == '"')
            buf[pos++] = '\\';
        buf[pos++] = *text;
    }
    if (pos + 1 < size)
        buf[pos++] = '"';
    buf[pos] = '\0';
    return pos;
}

static void remote_page_get_cb(proto_t *proto)
{
    host_remote_page_t page;
    char buffer[SOCKET_MSG_BUFFER_SIZE * 4];
    size_t pos;
    int resp, i;

    memset(&page, 0, sizeof(page));
    if (g_plugin_info && g_plugin_info->remote_page_get)
        resp = g_plugin_info->remote_page_get(atoi(proto->list[1]), atoi(proto->list[2]), &page);
    else
        resp = ERR_INVALID_OPERATION;

    if (resp < 0)
    {
        protocol_response_int(resp, proto);
        return;
    }

    pos = snprintf(buffer, sizeof(buffer), "resp 0 %u", page.id);
    pos = append_quoted(buffer, pos, sizeof(buffer), page.section);
    pos = append_quoted(buffer, pos, sizeof(buffer), page.name);
    for (i = 0; i < HOST_REMOTE_PAGE_SLOTS; i++)
        pos += snprintf(buffer + pos, sizeof(buffer) - pos, " %s", page.slots[i][0] ? page.slots[i] : "-");
    protocol_response(buffer, proto);
}

/* the shortest decimal that reads back to the same double, written as ECMAScript's Number toString does
   (no exponent from 1e-6 up to 1e21); -0 is written 0 */
static void format_number(char *buf, size_t size, double value)
{
    char tmp[40], digits[20];
    int precision, exponent, n, i;
    size_t pos = 0;

    if (value == 0.0)
    {
        snprintf(buf, size, "0");
        return;
    }
    for (precision = 0; precision < 17; precision++)
    {
        snprintf(tmp, sizeof(tmp), "%.*e", precision, value);
        if (strtod(tmp, NULL) == value)
            break;
    }

    /* tmp is [-]d[.ddd]e<exp>: split the digits and the exponent */
    n = 0;
    for (i = (tmp[0] == '-'); tmp[i] != 'e'; i++)
        if (tmp[i] != '.')
            digits[n++] = tmp[i];
    digits[n] = '\0';
    exponent = atoi(tmp + i + 1);
    while (n > 1 && digits[n - 1] == '0')
        digits[--n] = '\0';

    /* at most 26 bytes: a sign, "0.", six zeros and 17 digits */
    if (value < 0)
        buf[pos++] = '-';
    if (exponent >= 21 || exponent < -6)
    {
        buf[pos++] = digits[0];
        if (n > 1)
            pos += snprintf(buf + pos, size - pos, ".%s", digits + 1);
        snprintf(buf + pos, size - pos, "e%c%d", exponent < 0 ? '-' : '+', abs(exponent));
        return;
    }
    if (exponent < 0)
    {
        buf[pos++] = '0';
        buf[pos++] = '.';
        for (i = -1; i > exponent; i--)
            buf[pos++] = '0';
        snprintf(buf + pos, size - pos, "%s", digits);
        return;
    }
    for (i = 0; i < n || i <= exponent; i++)
    {
        if (i == exponent + 1)
            buf[pos++] = '.';
        buf[pos++] = i < n ? digits[i] : '0';
    }
    buf[pos] = '\0';
}

static void param_info_cb(proto_t *proto)
{
    host_param_info_t info;
    char numbers[4][64];
    char buffer[SOCKET_MSG_BUFFER_SIZE];
    int resp;

    memset(&info, 0, sizeof(info));
    if (g_plugin_info && g_plugin_info->param_info)
        resp = g_plugin_info->param_info(atoi(proto->list[1]), proto->list[2], &info);
    else
        resp = ERR_INVALID_OPERATION;

    if (resp < 0)
    {
        protocol_response_int(resp, proto);
        return;
    }

    format_number(numbers[0], sizeof(numbers[0]), info.min);
    format_number(numbers[1], sizeof(numbers[1]), info.max);
    format_number(numbers[2], sizeof(numbers[2]), info.def);
    format_number(numbers[3], sizeof(numbers[3]), info.step);
    if (snprintf(buffer, sizeof(buffer), "resp 0 %s %s %s %s %s %s %s", info.unit, info.scale,
                 numbers[0], numbers[1], numbers[2], numbers[3], info.stable_symbol) >= (int)sizeof(buffer))
        protocol_response_int(ERR_INVALID_OPERATION, proto);
    else
        protocol_response(buffer, proto);
}


/*
************************************************************************************************************************
*           GLOBAL FUNCTIONS
************************************************************************************************************************
*/

void host_dispatch_register(const host_backend_t *backend)
{
    g_backend = backend;

    protocol_add_command(EFFECT_ADD, add_cb);
    protocol_add_command(EFFECT_REMOVE, remove_cb);
    protocol_add_command(EFFECT_PRESET_LOAD, preset_load_cb);
    protocol_add_command(EFFECT_CONNECT, connect_cb);
    protocol_add_command(EFFECT_DISCONNECT, disconnect_cb);
    protocol_add_command(EFFECT_BYPASS, bypass_cb);
    protocol_add_command(EFFECT_PARAM_SET, param_set_cb);
    protocol_add_command(EFFECT_PARAM_GET, param_get_cb);
    protocol_add_command(STATE_LOAD, state_load_cb);
    protocol_add_command(STATE_SAVE, state_save_cb);
}

void host_dispatch_register_monitor_output(int (*monitor_output)(int instance, const char *symbol))
{
    g_monitor_output = monitor_output;

    protocol_add_command(MONITOR_OUTPUT, monitor_output_cb);
}

void host_dispatch_register_plugin_info(const host_plugin_info_t *info)
{
    g_plugin_info = info;

    protocol_add_command(TRACK_INFO, track_info_cb);
    protocol_add_command(REMOTE_PAGES, remote_pages_cb);
    protocol_add_command(REMOTE_PAGE_GET, remote_page_get_cb);
    protocol_add_command(PARAM_INFO, param_info_cb);
}

/* Every other command of mod-host.h answers ERR_INVALID_OPERATION, so a client that speaks the
   whole protocol to a host with only the backend table behind it never reads a grammar error. */
void host_dispatch_register_unsupported(void)
{
    static const char *const commands[] = {
        EFFECT_PRESET_SAVE, EFFECT_PRESET_SHOW, EFFECT_PARAM_MON, EFFECT_PATCH_GET, EFFECT_PATCH_SET,
        EFFECT_LICENSEE, EFFECT_SET_BPM, EFFECT_SET_BPB, MONITOR_ADDR_SET, MONITOR_OUTPUT,
        MONITOR_MIDI_PROGRAM, MIDI_LEARN, MIDI_MAP, MIDI_UNMAP, CC_MAP, CC_VALUE_SET, CC_UNMAP, CV_MAP,
        CV_UNMAP, HMI_MAP, HMI_UNMAP, LOAD_COMMANDS, SAVE_COMMANDS, BUNDLE_ADD, BUNDLE_REMOVE,
        STATE_TMPDIR, FEATURE_ENABLE, TRANSPORT, TRANSPORT_SYNC, SHOW_EXTERNAL_UI, OUTPUT_DATA_READY,
        TRACK_INFO, REMOTE_PAGES, REMOTE_PAGE_GET, PARAM_INFO,
        NULL
    };
    const char *const *command;

    for (command = commands; *command; command++)
        protocol_add_command(*command, host_dispatch_unsupported_cb);
}

void host_dispatch_unsupported_cb(proto_t *proto)
{
    protocol_response_int(ERR_INVALID_OPERATION, proto);
}

int host_dispatch_output_set(int instance, const char *symbol, float value)
{
    char buffer[SOCKET_MSG_BUFFER_SIZE];
    int len = snprintf(buffer, sizeof(buffer), OUTPUT_SET, instance, symbol, value);

    if (len < 0 || len >= (int)sizeof(buffer))
        return -1;

    return socket_send_feedback(buffer);
}

int host_dispatch_remote_pages_changed(int instance)
{
    char buffer[64];

    snprintf(buffer, sizeof(buffer), REMOTE_PAGES_CHANGED, instance);
    return socket_send_feedback(buffer);
}
