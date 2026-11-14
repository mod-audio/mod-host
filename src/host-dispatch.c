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
