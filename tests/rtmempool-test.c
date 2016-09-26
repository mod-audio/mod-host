
#include "../src/mod-semaphore.h"

#define RTMEMPOOL_REAL_TEST
#define ENABLE_POSTPONED_CACHE

#include "../src/rtmempool/list.h"

#ifdef RTMEMPOOL_REAL_TEST
#include "../src/rtmempool/rtmempool.c"
#else
#define rtsafe_memory_pool_deallocate(X,arg) free(arg)
#define rtsafe_memory_pool_allocate_atomic(X) malloc(sizeof(postponed_event_list_data))
#define rtsafe_memory_pool_destroy(X)
#define rtsafe_memory_pool_create(A,B,C,D,E) (1)
#define RtMemPool_Handle void*
#define RtMemPool void*
#endif

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <signal.h>
#include <pthread.h>

#define MAX_POSTPONED_EVENTS    1024

// used for local stack variables
#define MAX_CHAR_BUF_SIZE       255

#define UNUSED_PARAM(var)       do { (void)(var); } while (0)

enum PostPonedEventType {
    POSTPONED_PARAM_SET,
    POSTPONED_OUTPUT_MONITOR,
    POSTPONED_PROGRAM_LISTEN,
    POSTPONED_MIDI_MAP
};

typedef struct POSTPONED_EVENT_T {
    enum PostPonedEventType etype;
    int effect_id;
    const char* symbol;
    int8_t channel;
    int8_t controller;
    float value;
} postponed_event_t;

typedef struct POSTPONED_EVENT_LIST_DATA {
    postponed_event_t event;
    struct list_head siblings;
} postponed_event_list_data;

#ifdef ENABLE_POSTPONED_CACHE
typedef struct POSTPONED_CACHED_SYMBOL_LIST_DATA {
    int effect_id;
    char symbol[MAX_CHAR_BUF_SIZE+1];
    struct list_head siblings;
} postponed_cached_symbol_list_data;

typedef struct POSTPONED_CACHED_EVENTS {
    int last_effect_id;
    char last_symbol[MAX_CHAR_BUF_SIZE+1];
    postponed_cached_symbol_list_data symbols;
} postponed_cached_events;
#endif

static struct list_head g_rtsafe_list;
static RtMemPool_Handle g_rtsafe_mem_pool;
static pthread_mutex_t  g_rtsafe_mutex;

static volatile int  g_postevents_running; // 0: stopped, 1: running, -1: stopped & about to close mod-host
static volatile bool g_postevents_ready;
static sem_t         g_postevents_semaphore;
static pthread_t     g_postevents_thread;

int socket_send_feedback(const char *buffer);

#ifdef ENABLE_POSTPONED_CACHE
static bool ShouldIgnorePostPonedEvent(postponed_event_list_data* ev, postponed_cached_events* cached_events)
{
    // we don't have symbol-less events that need caching
    if (ev->event.symbol == NULL)
        return false;

    if (ev->event.effect_id == cached_events->last_effect_id &&
        strncmp(ev->event.symbol, cached_events->last_symbol, MAX_CHAR_BUF_SIZE) == 0)
    {
        // already received this event, like just now
        return true;
    }

    // we received some events, but last one was different
    // we might be getting interleaved events, so let's check if it's there
    struct list_head *it;
    list_for_each(it, &cached_events->symbols.siblings)
    {
        postponed_cached_symbol_list_data* const psymbol = list_entry(it, postponed_cached_symbol_list_data, siblings);

        if (ev->event.effect_id == psymbol->effect_id &&
            strncmp(ev->event.symbol, psymbol->symbol, MAX_CHAR_BUF_SIZE) == 0)
        {
            // haha! found you little bastard!
            return true;
        }
    }

    // we'll process this event because it's the "last" of its type
    // also add it to the list of received events
    postponed_cached_symbol_list_data* const psymbol = malloc(sizeof(postponed_cached_symbol_list_data));

    if (psymbol)
    {
        psymbol->effect_id = ev->event.effect_id;
        strncpy(psymbol->symbol, ev->event.symbol, MAX_CHAR_BUF_SIZE);
        psymbol->symbol[MAX_CHAR_BUF_SIZE] = '\0';
        list_add_tail(&psymbol->siblings, &cached_events->symbols.siblings);
    }

    return false;
}
#endif

void RunPostPonedEvents(int ignored_effect_id)
{
    // local queue to where we'll save rtsafe list
    struct list_head queue;
    INIT_LIST_HEAD(&queue);

    // move rtsafe list to our local queue, and clear it
    pthread_mutex_lock(&g_rtsafe_mutex);
    list_splice_init(&g_rtsafe_list, &queue);
    pthread_mutex_unlock(&g_rtsafe_mutex);

    RtMemPool* poolPtr = g_rtsafe_mem_pool;

    if (ignored_effect_id >= 0)
    {
        printf("flushing events 001\n");
#ifdef RTMEMPOOL_REAL_TEST
        printf("STEP0 usedCount %i\n", poolPtr->usedCount);
        printf("STEP0 unusedCount %i\n", poolPtr->unusedCount);
#endif
    }

    if (list_empty(&queue))
    {
        if (ignored_effect_id >= 0) printf("flushing events 002\n");
        // nothing to do
        return;
    }
    if (ignored_effect_id >= 0) printf("flushing events 003\n");

    // local buffer
    char buf[MAX_CHAR_BUF_SIZE+1];
    buf[MAX_CHAR_BUF_SIZE] = '\0';

#ifdef ENABLE_POSTPONED_CACHE
    // cached data, to make sure we only handle similar events once
    bool got_midi_program = false;
    postponed_cached_events cached_param_set, cached_output_mon;
    postponed_cached_symbol_list_data *psymbol;

    cached_param_set.last_effect_id = -1;
    cached_output_mon.last_effect_id = -1;
    cached_param_set.last_symbol[MAX_CHAR_BUF_SIZE] = '\0';
    cached_output_mon.last_symbol[MAX_CHAR_BUF_SIZE] = '\0';
    INIT_LIST_HEAD(&cached_param_set.symbols.siblings);
    INIT_LIST_HEAD(&cached_output_mon.symbols.siblings);
#endif

    // itenerate backwards
    struct list_head *it, *it2;
    postponed_event_list_data* eventptr;

    list_for_each_prev(it, &queue)
    {
        eventptr = list_entry(it, postponed_event_list_data, siblings);
        if (ignored_effect_id >= 0) printf("flushing events 004 %p\n", eventptr);

        // do not handle events for a plugin that is about to be removed
        if (eventptr->event.effect_id == ignored_effect_id)
        {
            if (ignored_effect_id >= 0) printf("flushing events 004 found\n");
            continue;
        }

        switch (eventptr->event.etype)
        {
        case POSTPONED_PARAM_SET:
#ifdef ENABLE_POSTPONED_CACHE
            if (ShouldIgnorePostPonedEvent(eventptr, &cached_param_set))
                continue;
#endif

            snprintf(buf, MAX_CHAR_BUF_SIZE, "param_set %i %s %f", eventptr->event.effect_id,
                                                                   eventptr->event.symbol,
                                                                   eventptr->event.value);
            socket_send_feedback(buf);

#ifdef ENABLE_POSTPONED_CACHE
            // save for fast checkup next time
            cached_param_set.last_effect_id = eventptr->event.effect_id;
            strncpy(cached_param_set.last_symbol, eventptr->event.symbol, MAX_CHAR_BUF_SIZE);
#endif
            break;

        case POSTPONED_OUTPUT_MONITOR:
#ifdef ENABLE_POSTPONED_CACHE
            if (ShouldIgnorePostPonedEvent(eventptr, &cached_output_mon))
                continue;
#endif

            snprintf(buf, MAX_CHAR_BUF_SIZE, "output_set %i %s %f", eventptr->event.effect_id,
                                                                    eventptr->event.symbol,
                                                                    eventptr->event.value);
            socket_send_feedback(buf);

#ifdef ENABLE_POSTPONED_CACHE
            // save for fast checkup next time
            cached_output_mon.last_effect_id = eventptr->event.effect_id;
            strncpy(cached_output_mon.last_symbol, eventptr->event.symbol, MAX_CHAR_BUF_SIZE);
#endif
            break;

        case POSTPONED_PROGRAM_LISTEN:
#ifdef ENABLE_POSTPONED_CACHE
            if (got_midi_program)
                continue;
#endif

            snprintf(buf, MAX_CHAR_BUF_SIZE, "midi_program %i", eventptr->event.controller);
            socket_send_feedback(buf);

#ifdef ENABLE_POSTPONED_CACHE
            // ignore next midi program
            got_midi_program = true;
#endif
            break;

        case POSTPONED_MIDI_MAP:
            snprintf(buf, MAX_CHAR_BUF_SIZE, "midi_mapped %i %s %i %i %f", eventptr->event.effect_id,
                                                                           eventptr->event.symbol,
                                                                           eventptr->event.channel,
                                                                           eventptr->event.controller,
                                                                           eventptr->event.value);
            socket_send_feedback(buf);
            break;
        }
    }

#ifdef ENABLE_POSTPONED_CACHE
    // cleanup memory
    list_for_each_safe(it, it2, &cached_param_set.symbols.siblings)
    {
        psymbol = list_entry(it, postponed_cached_symbol_list_data, siblings);
        free(psymbol);
    }
    list_for_each_safe(it, it2, &cached_output_mon.symbols.siblings)
    {
        psymbol = list_entry(it, postponed_cached_symbol_list_data, siblings);
        free(psymbol);
    }
#endif
    // cleanup memory of rt queue, safely
    list_for_each_safe(it, it2, &queue)
    {
        eventptr = list_entry(it, postponed_event_list_data, siblings);
        rtsafe_memory_pool_deallocate(g_rtsafe_mem_pool, eventptr);
    }

    if (ignored_effect_id >= 0)
    {
        printf("flushing events END!\n");
#ifdef RTMEMPOOL_REAL_TEST
        printf("STEP0 usedCount %i\n", poolPtr->usedCount);
        printf("STEP0 unusedCount %i\n", poolPtr->unusedCount);
#endif
    }

    if (g_postevents_ready)
    {
        // report data finished to server
        g_postevents_ready = false;
        socket_send_feedback("data_finish");
    }
}

static void* PostPonedEventsThread(void* arg)
{
    while (g_postevents_running == 1)
    {
        if (sem_timedwait_secs(&g_postevents_semaphore, 1) != 0)
            continue;

        if (g_postevents_running == 1 && g_postevents_ready)
            RunPostPonedEvents(-3); // as all effects are valid we set ignored_effect_id to -3
    }

    return NULL;

    UNUSED_PARAM(arg);
}

static void JackThreadInit(void* arg)
{
/* Disable denormal numbers in floating point calculation.
 * Denormal numbers happen often in IIR filters, and it can be very slow.
 */
/* Taken from cras/src/dsp/dsp_util.c in Chromium OS code.
 * Copyright (c) 2013 The Chromium OS Authors. */
#if defined(__i386__) || defined(__x86_64__)
        unsigned int mxcsr;
        mxcsr = __builtin_ia32_stmxcsr();
        __builtin_ia32_ldmxcsr(mxcsr | 0x8040);
#elif defined(__aarch64__)
        uint64_t cw;
        __asm__ __volatile__ (
                "mrs    %0, fpcr                            \n"
                "orr    %0, %0, #0x1000000                  \n"
                "msr    fpcr, %0                            \n"
                "isb                                        \n"
                : "=r"(cw) :: "memory");
#elif defined(__arm__)
        uint32_t cw;
        __asm__ __volatile__ (
                "vmrs   %0, fpscr                           \n"
                "orr    %0, %0, #0x1000000                  \n"
                "vmsr   fpscr, %0                           \n"
                : "=r"(cw) :: "memory");
#else
#warning "Don't know how to disable denormals. Performace may suffer."
#endif
    return;

    UNUSED_PARAM(arg);
}

// ----------------------------------------------------------------------------------------------------------

typedef struct {
    int effect_id;
    const char* port_symbols[3];
    pthread_t _thread;
} PLUGIN;

static PLUGIN g_plugins[5];
static int g_process_usleep = 1000*1;

static void ProcessPlugin(PLUGIN *arg)
{
    float value;

    for (int i = 0; i < 3; i++)
    {
        value = i/1.3f;

        postponed_event_list_data* const posteventptr = rtsafe_memory_pool_allocate_atomic(g_rtsafe_mem_pool);

        if (posteventptr == NULL)
            continue;

        const postponed_event_t pevent = {
            POSTPONED_OUTPUT_MONITOR,
            arg->effect_id,
            arg->port_symbols[i],
            -1, -1,
            value
        };

        memcpy(&posteventptr->event, &pevent, sizeof(postponed_event_t));

        pthread_mutex_lock(&g_rtsafe_mutex);
        list_add_tail(&posteventptr->siblings, &g_rtsafe_list);
        pthread_mutex_unlock(&g_rtsafe_mutex);
    }

    sem_post(&g_postevents_semaphore);
}

static void* ProcessPluginLoop(void *arg)
{
    JackThreadInit(NULL);

    PLUGIN* plugin = (PLUGIN*)arg;

    while (plugin->effect_id >= 0)
    {
        ProcessPlugin(plugin);
        usleep(g_process_usleep);
    }

    return NULL;
}

// ----------------------------------------------------------------------------------------------------------

int socket_send_feedback(const char *buffer)
{
    //fprintf(stdout, "%s\n", buffer);
    return 0;

    UNUSED_PARAM(buffer);
}

int effects_add(int instance)
{
    if (instance < 0 || instance >= 5)
        return 1;

    PLUGIN* plugin = &g_plugins[instance];

    plugin->effect_id = instance;
    plugin->port_symbols[0] = strdup("port1");
    plugin->port_symbols[1] = strdup("port2 hey");
    plugin->port_symbols[2] = strdup("port3 sup");
    pthread_create(&plugin->_thread, NULL, ProcessPluginLoop, plugin);
    return 0;
}

int effects_remove(int effect_id)
{
    // stop postpone events thread
    if (g_postevents_running == 1)
    {
        g_postevents_running = 0;
        sem_post(&g_postevents_semaphore);
        pthread_join(g_postevents_thread, NULL);
    }

    // CUSTOM CLEANUP
    PLUGIN* plugin = &g_plugins[effect_id];
    plugin->effect_id = -1;
    pthread_join(plugin->_thread, NULL);
    free((void*)plugin->port_symbols[0]);
    free((void*)plugin->port_symbols[1]);
    free((void*)plugin->port_symbols[2]);

    // simulate jack_deactivate, no more processing
    usleep(g_process_usleep*3);

    // flush events for all effects except this one
    RunPostPonedEvents(effect_id);

    // start thread again
    if (g_postevents_running == 0)
    {
        g_postevents_running = 1;
        pthread_create(&g_postevents_thread, NULL, PostPonedEventsThread, NULL);
    }

    return 0;
}

// ----------------------------------------------------------------------------------------------------------

volatile bool running;

static void term_signal(int sig)
{
    running = false;

    /* unused */
    return; (void)sig;
}

int main(void)
{
    // -- INIT ----------------------------------------------------------------------------------------------

    INIT_LIST_HEAD(&g_rtsafe_list);

    if (!rtsafe_memory_pool_create(&g_rtsafe_mem_pool, "mod-host", sizeof(postponed_event_list_data),
                                   MAX_POSTPONED_EVENTS))
    {
        fprintf(stderr, "can't allocate realtime-safe memory pool\n");
        return 1;
    }

    RtMemPool* poolPtr = g_rtsafe_mem_pool;

    pthread_mutexattr_t atts;
    pthread_mutexattr_init(&atts);
#ifdef __ARM_ARCH_7A__
    pthread_mutexattr_setprotocol(&atts, PTHREAD_PRIO_INHERIT);
#endif
    pthread_mutex_init(&g_rtsafe_mutex, &atts);
    pthread_mutexattr_destroy(&atts);

    sem_init(&g_postevents_semaphore, 0, 0);

    g_postevents_running = 1;
    g_postevents_ready = true;
    pthread_create(&g_postevents_thread, NULL, PostPonedEventsThread, NULL);

#ifdef RTMEMPOOL_REAL_TEST
    printf("STEP1 usedCount %i\n", poolPtr->usedCount);
    printf("STEP1 unusedCount %i\n", poolPtr->unusedCount);
#endif

    // -- IDLE ----------------------------------------------------------------------------------------------

    running = true;

    struct sigaction sig;
    memset(&sig, 0, sizeof(sig));

    sig.sa_handler = term_signal;
    sig.sa_flags   = SA_RESTART;
    sigemptyset(&sig.sa_mask);
    sigaction(SIGTERM, &sig, NULL);
    sigaction(SIGINT, &sig, NULL);

    for (int i=0; i<5; ++i)
    {
#ifdef RTMEMPOOL_REAL_TEST
        printf("STEP2 p%i usedCount %i\n", i+1, poolPtr->usedCount);
        printf("STEP2 p%i unusedCount %i\n", i+1, poolPtr->unusedCount);
#endif
        effects_add(i);
    }

    while (running)
    {
        usleep(1000*5);
        g_postevents_ready = true;
    }

    for (int i=0; i<5; ++i)
    {
#ifdef RTMEMPOOL_REAL_TEST
        printf("STEP3 p%i usedCount %i\n", i+1, poolPtr->usedCount);
        printf("STEP3 p%i unusedCount %i\n", i+1, poolPtr->unusedCount);
#endif
        effects_remove(i);
    }

    // -- FINISH --------------------------------------------------------------------------------------------

    g_postevents_running = -1;
    sem_post(&g_postevents_semaphore);
    pthread_join(g_postevents_thread, NULL);

#ifdef RTMEMPOOL_REAL_TEST
    printf("STEP4 usedCount %i\n", poolPtr->usedCount);
    printf("STEP4 unusedCount %i\n", poolPtr->unusedCount);
#endif

    struct list_head *it, *it2;
    postponed_event_list_data* eventptr;

    list_for_each_safe(it, it2, &g_rtsafe_list)
    {
        eventptr = list_entry(it, postponed_event_list_data, siblings);
        printf("STEP5 eventptr = %p\n", eventptr);
        rtsafe_memory_pool_deallocate(g_rtsafe_mem_pool, eventptr);
    }

#ifdef RTMEMPOOL_REAL_TEST
    printf("STEP5 usedCount %i\n", poolPtr->usedCount);
    printf("STEP5 unusedCount %i\n", poolPtr->unusedCount);
#endif

    rtsafe_memory_pool_destroy(g_rtsafe_mem_pool);
    sem_destroy(&g_postevents_semaphore);
    pthread_mutex_destroy(&g_rtsafe_mutex);

    return 0;
}
