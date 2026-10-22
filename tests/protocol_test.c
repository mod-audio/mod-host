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

/* Drives the socket protocol end to end against a fake backend, no jack needed:
 * every reply is compared byte for byte and every backend call is recorded.
 * Then runs tests/host-scenarios.txt through the direct exchange against a
 * reference backend, the same way a host in one process is tested. */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/socket.h>
#include <netinet/in.h>

#include "../src/socket.h"
#include "../src/protocol.h"
#include "../src/host-dispatch.h"
#include "../src/host-errors.h"
#include "../src/host-scenario.h"

#define TEST_PORT_DEFAULT   15556
#define TEST_BUFFER_SIZE    1024

typedef struct EXCHANGE_T {
    const char *command;
    const char *reply;
} exchange_t;

typedef struct SESSION_T {
    int port;
    const exchange_t *exchanges;
} session_t;

static char g_calls[2048];
static int g_failures;

static void record(const char *call)
{
    strncat(g_calls, call, sizeof(g_calls) - strlen(g_calls) - 1);
}

static int fake_add(const char *uri, int instance, const char *client_name)
{
    char buf[256];
    snprintf(buf, sizeof(buf), "add(%s,%i,%s) ", uri, instance, client_name ? client_name : "-");
    record(buf);
    return instance;
}

static int fake_remove(int instance)
{
    char buf[64];
    snprintf(buf, sizeof(buf), "remove(%i) ", instance);
    record(buf);
    return SUCCESS;
}

static int fake_bypass(int instance, int value)
{
    char buf[64];
    snprintf(buf, sizeof(buf), "bypass(%i,%i) ", instance, value);
    record(buf);
    return SUCCESS;
}

static int fake_param_set(int instance, const char *symbol, float value)
{
    char buf[128];
    snprintf(buf, sizeof(buf), "param_set(%i,%s,%f) ", instance, symbol, value);
    record(buf);
    return SUCCESS;
}

static int fake_param_get(int instance, const char *symbol, float *value)
{
    char buf[128];
    snprintf(buf, sizeof(buf), "param_get(%i,%s) ", instance, symbol);
    record(buf);
    *value = 0.5f;
    return SUCCESS;
}

static int fake_preset_load(int instance, const char *uri)
{
    char buf[128];
    snprintf(buf, sizeof(buf), "preset_load(%i,%s) ", instance, uri);
    record(buf);
    return ERR_LV2_INVALID_PRESET_URI;
}

static int fake_state_save(const char *dir)
{
    char buf[128];
    snprintf(buf, sizeof(buf), "state_save(%s) ", dir);
    record(buf);
    return SUCCESS;
}

static int fake_state_load(const char *dir)
{
    char buf[128];
    snprintf(buf, sizeof(buf), "state_load(%s) ", dir);
    record(buf);
    return ERR_LV2_CANT_LOAD_STATE;
}

static int fake_connect(const char *port_a, const char *port_b)
{
    char buf[128];
    snprintf(buf, sizeof(buf), "connect(%s,%s) ", port_a, port_b);
    record(buf);
    return SUCCESS;
}

static int fake_disconnect(const char *port_a, const char *port_b)
{
    char buf[128];
    snprintf(buf, sizeof(buf), "disconnect(%s,%s) ", port_a, port_b);
    record(buf);
    return ERR_JACK_PORT_DISCONNECTION;
}

static const host_backend_t g_fake_backend = {
    fake_add,
    fake_remove,
    fake_bypass,
    fake_param_set,
    fake_param_get,
    fake_preset_load,
    fake_state_save,
    fake_state_load,
    fake_connect,
    fake_disconnect,
};

static const exchange_t g_fake_exchanges[] = {
    { "add http://x/y 3 chan1",     "resp 3" },
    { "add http://x/y 4",           "resp 4" },
    { "bypass 3 1",                 "resp 0" },
    { "param_set 3 gain 0.5",       "resp 0" },
    { "param_get 3 gain",           "resp 0 0.5000" },
    { "preset_load 3 urn:p",        "resp -104" },
    { "connect a:out b:in",         "resp 0" },
    { "disconnect a:out b:in",      "resp -206" },
    { "state_save /tmp/s",          "resp 0" },
    { "state_load /tmp/s",          "resp -105" },
    { "remove 3",                   "resp 0" },
    { "licensee 3",                 "resp -902" },
    { "cpu_load",                   "not found" },
    { "param_get 3",                "few arguments" },
    { NULL, NULL }
};

static const char g_fake_calls[] =
    "add(http://x/y,3,chan1) add(http://x/y,4,-) bypass(3,1) param_set(3,gain,0.500000) "
    "param_get(3,gain) preset_load(3,urn:p) connect(a:out,b:in) disconnect(a:out,b:in) "
    "state_save(/tmp/s) state_load(/tmp/s) remove(3) ";

/* a backend with nothing behind it answers every verb with ERR_INVALID_OPERATION */
static const host_backend_t g_empty_backend = {
    NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
};

static const exchange_t g_empty_exchanges[] = {
    { "add http://x/y 3",           "resp -902" },
    { "param_get 3 gain",           "resp -902" },
    { "state_save /tmp/s",          "resp -902" },
    { NULL, NULL }
};

/* A reference backend: one plugin, one instance table, one control, bypass as the host's. */
#define REF_URI         "urn:ref:plugin"
#define REF_INSTANCES   16

static struct {
    int present;
    int bypass;
    float gain;
} g_ref[REF_INSTANCES];

static int ref_add(const char *uri, int instance, const char *client_name)
{
    (void)client_name;
    if (instance < 0 || instance >= REF_INSTANCES)
        return ERR_INSTANCE_INVALID;
    if (g_ref[instance].present)
        return ERR_INSTANCE_ALREADY_EXISTS;
    if (strcmp(uri, REF_URI) != 0)
        return ERR_HOST_INVALID_URI;
    g_ref[instance].present = 1;
    g_ref[instance].bypass = 0;
    g_ref[instance].gain = 1.0f;
    return instance;
}

static int ref_remove(int instance)
{
    if (instance < 0 || instance >= REF_INSTANCES || !g_ref[instance].present)
        return ERR_INSTANCE_NON_EXISTS;
    g_ref[instance].present = 0;
    return SUCCESS;
}

static int ref_bypass(int instance, int value)
{
    if (instance < 0 || instance >= REF_INSTANCES || !g_ref[instance].present)
        return ERR_INSTANCE_NON_EXISTS;
    g_ref[instance].bypass = value != 0;
    return SUCCESS;
}

static int ref_param_set(int instance, const char *symbol, float value)
{
    if (instance < 0 || instance >= REF_INSTANCES || !g_ref[instance].present)
        return ERR_INSTANCE_NON_EXISTS;
    if (strcmp(symbol, ":bypass") == 0)
        return ref_bypass(instance, value > 0.5f);
    if (strcmp(symbol, "gain") != 0)
        return ERR_HOST_INVALID_PARAM_SYMBOL;
    g_ref[instance].gain = value;
    return SUCCESS;
}

static int ref_param_get(int instance, const char *symbol, float *value)
{
    if (instance < 0 || instance >= REF_INSTANCES || !g_ref[instance].present)
        return ERR_INSTANCE_NON_EXISTS;
    if (strcmp(symbol, ":bypass") == 0)
    {
        *value = g_ref[instance].bypass ? 1.0f : 0.0f;
        return SUCCESS;
    }
    if (strcmp(symbol, "gain") != 0)
        return ERR_HOST_INVALID_PARAM_SYMBOL;
    *value = g_ref[instance].gain;
    return SUCCESS;
}

static int ref_preset_load(int instance, const char *uri)
{
    (void)uri;
    if (instance < 0 || instance >= REF_INSTANCES || !g_ref[instance].present)
        return ERR_INSTANCE_NON_EXISTS;
    return ERR_HOST_INVALID_PRESET_URI;
}

static float g_ref_saved[REF_INSTANCES];

static int ref_state_save(const char *dir)
{
    int i;
    (void)dir;
    for (i = 0; i < REF_INSTANCES; i++)
        g_ref_saved[i] = g_ref[i].gain;
    return SUCCESS;
}

static int ref_state_load(const char *dir)
{
    int i;
    (void)dir;
    for (i = 0; i < REF_INSTANCES; i++)
        if (g_ref[i].present)
            g_ref[i].gain = g_ref_saved[i];
    return SUCCESS;
}

static const host_backend_t g_ref_backend = {
    .add = ref_add,
    .remove = ref_remove,
    .bypass = ref_bypass,
    .param_set = ref_param_set,
    .param_get = ref_param_get,
    .preset_load = ref_preset_load,
    .state_save = ref_state_save,
    .state_load = ref_state_load,
    .connect = fake_connect,
    .disconnect = fake_disconnect,
};

/* the same with no presets, no state and no wiring: the runner must read -902 and skip */
static const host_backend_t g_ref_bare_backend = {
    .add = ref_add,
    .remove = ref_remove,
    .bypass = ref_bypass,
    .param_set = ref_param_set,
    .param_get = ref_param_get,
};

static const host_scenario_var_t g_ref_vars[] = {
    { "URI", REF_URI },
    { "BAD_URI", "urn:ref:missing" },
    { "PARAM", "gain" },
    { "DIR", "/tmp" },
    { "PORT_A", "a:out" },
    { "PORT_B", "b:in" },
    { "REMOVE_TWICE", "resp -3" },
};

static int run_scenarios(const char *path, const host_backend_t *backend)
{
    host_scenario_list_t list;
    host_scenario_direct_t direct;
    int failures;

    memset(g_ref, 0, sizeof(g_ref));
    if (host_scenario_load(path, g_ref_vars, 7, &list) <= 0)
    {
        printf("FAIL cannot load %s\n", path);
        return 1;
    }
    if (host_scenario_direct_open(&direct, backend) != 0)
    {
        printf("FAIL direct exchange\n");
        host_scenario_free(&list);
        return 1;
    }
    failures = host_scenario_run(&list, host_scenario_direct_exchange, &direct, backend);
    host_scenario_direct_close(&direct);
    host_scenario_free(&list);
    if (failures != 0)
    {
        printf("FAIL scenarios: %d\n", failures);
        return 1;
    }
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

static void *client_thread(void *arg)
{
    const session_t *session = arg;
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in addr;
    char reply[TEST_BUFFER_SIZE];

    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(session->port);
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

    if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0)
    {
        perror("connect");
        g_failures++;
        close(fd);
        return NULL;
    }

    for (const exchange_t *ex = session->exchanges; ex->command; ex++)
    {
        if (send(fd, ex->command, strlen(ex->command), 0) < 0 || read_reply(fd, reply, sizeof(reply)) != 0)
        {
            printf("FAIL '%s': no reply\n", ex->command);
            g_failures++;
            break;
        }
        if (strcmp(reply, ex->reply) != 0)
        {
            printf("FAIL '%s': got '%s', want '%s'\n", ex->command, reply, ex->reply);
            g_failures++;
        }
        else
        {
            printf("ok   '%s' -> '%s'\n", ex->command, reply);
        }
    }

    close(fd);
    return NULL;
}

static int g_idle_calls;

static void count_idle(void)
{
    g_idle_calls++;
}

static void *idle_client_thread(void *arg)
{
    const session_t *session = arg;
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in addr;

    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(session->port);
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

    if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0)
    {
        perror("connect");
        g_failures++;
        close(fd);
        return NULL;
    }
    usleep(100000);
    close(fd);
    return NULL;
}

/* A quiet client for 100 ms with a 10 ms idle interval: the idle callback runs
 * with no message arriving, at least 5 times. */
static int run_idle_session(int port)
{
    session_t session = { port, NULL };
    pthread_t thread;

    if (socket_start(port, 0, TEST_BUFFER_SIZE) < 0)
    {
        printf("FAIL socket_start on port %i\n", port);
        return 1;
    }

    g_idle_calls = 0;
    socket_set_idle_cb(count_idle);
    socket_set_idle_interval(10);

    pthread_create(&thread, NULL, idle_client_thread, &session);
    socket_run(0);
    pthread_join(thread, NULL);

    socket_set_idle_interval(0);
    socket_set_idle_cb(NULL);
    socket_finish();

    if (g_idle_calls >= 5)
    {
        printf("ok   %d idle calls in 100 ms without a message\n", g_idle_calls);
    }
    else
    {
        printf("FAIL %d idle calls in 100 ms without a message, want at least 5\n", g_idle_calls);
        g_failures++;
    }

    return 0;
}

static int run_session(int port, const host_backend_t *backend, const exchange_t *exchanges, const char *expected_calls)
{
    session_t session = { port, exchanges };
    pthread_t thread;

    g_calls[0] = '\0';

    if (socket_start(port, 0, TEST_BUFFER_SIZE) < 0)
    {
        printf("FAIL socket_start on port %i\n", port);
        return 1;
    }

    socket_set_receive_cb(protocol_parse);
    host_dispatch_register(backend);
    protocol_add_command("licensee %i", host_dispatch_unsupported_cb);

    pthread_create(&thread, NULL, client_thread, &session);
    socket_run(0);
    pthread_join(thread, NULL);

    socket_finish();
    protocol_remove_commands();

    if (strcmp(g_calls, expected_calls) != 0)
    {
        printf("FAIL calls: got '%s', want '%s'\n", g_calls, expected_calls);
        g_failures++;
    }

    return 0;
}

int main(void)
{
    const char *env = getenv("PROTOCOL_TEST_PORT");
    int port = env ? atoi(env) : TEST_PORT_DEFAULT;

    if (run_session(port, &g_fake_backend, g_fake_exchanges, g_fake_calls) != 0)
        return 1;
    if (run_session(port, &g_empty_backend, g_empty_exchanges, "") != 0)
        return 1;
    if (run_idle_session(port) != 0)
        return 1;

    const char *scenarios = getenv("HOST_SCENARIOS");
    if (!scenarios)
        scenarios = "tests/host-scenarios.txt";
    if (run_scenarios(scenarios, &g_ref_backend) != 0)
        g_failures++;
    if (run_scenarios(scenarios, &g_ref_bare_backend) != 0)
        g_failures++;

    printf("%s\n", g_failures == 0 ? "protocol test ok" : "protocol test FAILED");
    return g_failures == 0 ? 0 : 1;
}
