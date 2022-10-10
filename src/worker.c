/*
  Copyright 2007-2012 David Robillard <http://drobilla.net>

  Permission to use, copy, modify, and/or distribute this software for any
  purpose with or without fee is hereby granted, provided that the above
  copyright notice and this permission notice appear in all copies.

  THIS SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
  WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
  MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
  ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
  WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
  ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
  OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
*/

#include <stdlib.h>

#include "worker.h"

static LV2_Worker_Status worker_respond(LV2_Worker_Respond_Handle handle, uint32_t size, const void* data)
{
    worker_t* worker = (worker_t*)handle;
    if (sizeof(size) + size > jack_ringbuffer_write_space(worker->responses))
        return LV2_WORKER_ERR_NO_SPACE;
    jack_ringbuffer_write(worker->responses, (const char*)&size, sizeof(size));
    jack_ringbuffer_write(worker->responses, (const char*)data, size);
    return LV2_WORKER_SUCCESS;
}

static void* worker_func(void* data)
{
    worker_t* worker = (worker_t*)data;
    void* buf = NULL;
    while (true) {
        sem_wait(&worker->sem);
        if (worker->exit) break;

        while (jack_ringbuffer_read_space(worker->requests) != 0) {
            uint32_t size = 0;
            jack_ringbuffer_read(worker->requests, (char*)&size, sizeof(size));

            if (!(buf = realloc(buf, size))) {
                fprintf(stderr, "worker_func: realloc() failed\n");
                free(buf);
                return NULL;
            }

            jack_ringbuffer_read(worker->requests, (char*)buf, size);
            worker->iface->work(worker->instance->lv2_handle, worker_respond, worker, size, buf);
        }
    }

    free(buf);
    return NULL;
}

void worker_init(worker_t *worker, LilvInstance *instance, const LV2_Worker_Interface *iface, uint32_t size)
{
    worker->exit = false;
    worker->iface = iface;
    worker->instance = instance;
    sem_init(&worker->sem, 0, 0);
    zix_thread_create(&worker->thread, size + sizeof(void*) * 4, worker_func, worker);
    worker->requests  = jack_ringbuffer_create(size);
    worker->responses = jack_ringbuffer_create(size);
    worker->response  = malloc(size);
    jack_ringbuffer_mlock(worker->requests);
    jack_ringbuffer_mlock(worker->responses);
}

void worker_finish(worker_t *worker)
{
    worker->exit = true;
    if (worker->requests) {
        sem_post(&worker->sem);
        zix_thread_join(worker->thread, NULL);
        jack_ringbuffer_free(worker->requests);
        jack_ringbuffer_free(worker->responses);
        free(worker->response);
    }
}

LV2_Worker_Status worker_schedule(LV2_Worker_Schedule_Handle handle, uint32_t size, const void *data)
{
    worker_t* worker = (worker_t*) handle;
    if (sizeof(size) + size > jack_ringbuffer_write_space(worker->requests))
        return LV2_WORKER_ERR_NO_SPACE;
    jack_ringbuffer_write(worker->requests, (const char*)&size, sizeof(size));
    jack_ringbuffer_write(worker->requests, (const char*)data, size);
    sem_post(&worker->sem);
    return LV2_WORKER_SUCCESS;
}

void worker_emit_responses(worker_t *worker)
{
    if (worker->responses) {
        uint32_t read_space = jack_ringbuffer_read_space(worker->responses);
        while (read_space) {
            uint32_t size = 0;
            jack_ringbuffer_read(worker->responses, (char*)&size, sizeof(size));
            jack_ringbuffer_read(worker->responses, (char*)worker->response, size);

            worker->iface->work_response(worker->instance->lv2_handle, size, worker->response);
            read_space -= sizeof(size) + size;
        }
    }
}
