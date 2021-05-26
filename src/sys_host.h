/*
 * This file is part of mod-system-control.
 */

#pragma once

#define SYS_SERIAL_SHM "/sys_msgs"

// #include "lv2-hmi.h"

#include "mod-semaphore.h"

#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

// must be 8192 - sizeof sys_serial_shm_data members, so we cleanly align to 64bits
#define SYS_SERIAL_SHM_DATA_SIZE (8192 - sizeof(sem_t) - sizeof(uint32_t)*2)

// using invalid ascii characters as to not conflict with regular text contents
typedef enum {
    sys_serial_event_type_null = 0,
    sys_serial_event_type_led = 0x80 + 'l',
    sys_serial_event_type_name = 0x80 + 'n',
    sys_serial_event_type_value = 0x80 + 'v',
    sys_serial_event_type_unit = 0x80 + 'u',
    sys_serial_event_type_widget_indicator = 0x80 + 'i'
} sys_serial_event_type;

typedef struct {
    // semaphore for syncing
    sem_t sem;
    // for ringbuffer-like access
    uint32_t head, tail;
    // actual data buffer
    uint8_t buffer[SYS_SERIAL_SHM_DATA_SIZE];
} sys_serial_shm_data;

static inline
bool sys_serial_open(int* shmfd, sys_serial_shm_data** data)
{
    int fd;
#ifdef SERVER_MODE
    sem_t sem;
#endif
    sys_serial_shm_data* ptr;

#ifdef SERVER_MODE
    // always close in case process crash
    shm_unlink(SYS_SERIAL_SHM);
    // this should work now..
    fd = shm_open(SYS_SERIAL_SHM, O_CREAT|O_EXCL|O_RDWR, 0600);
#else
    fd = shm_open(SYS_SERIAL_SHM, O_RDWR, 0);
#endif

    if (fd < 0)
    {
        fprintf(stderr, "shm_open failed\n");
        return false;
    }

#ifdef SERVER_MODE
    if (ftruncate(fd, sizeof(sys_serial_shm_data)) != 0)
    {
        fprintf(stderr, "ftruncate failed\n");
        goto cleanup;
    }
    if (sem_init(&sem, 1, 0) != 0)
    {
        fprintf(stderr, "sem_init failed\n");
        goto cleanup;
    }
#endif

    ptr = (sys_serial_shm_data*)mmap(NULL,
                                     sizeof(sys_serial_shm_data),
                                     PROT_READ|PROT_WRITE,
                                     MAP_SHARED|MAP_LOCKED,
                                     fd,
                                     0);

    if (ptr == NULL || ptr == MAP_FAILED)
    {
        fprintf(stderr, "mmap failed\n");
        goto cleanup_sem;
    }

#ifdef SERVER_MODE
    memset(ptr, 0, sizeof(sys_serial_shm_data));
    ptr->sem = sem;
#endif

    *shmfd = fd;
    *data = ptr;
    return true;

cleanup_sem:
#ifdef SERVER_MODE
    sem_destroy(&sem);
#endif

#ifdef SERVER_MODE
cleanup:
#endif
    close(fd);
#ifdef SERVER_MODE
    shm_unlink(SYS_SERIAL_SHM);
#endif

    *shmfd = 0;
    *data = NULL;
    return false;
}

static inline
void sys_serial_close(int shmfd, sys_serial_shm_data* data)
{
#ifdef SERVER_MODE
    sem_destroy(&data->sem);
#endif
    munmap(data, sizeof(sys_serial_shm_data));

    close(shmfd);
#ifdef SERVER_MODE
    shm_unlink(SYS_SERIAL_SHM);
#endif
}

// server, read must only be a result of a semaphore post action
static inline
bool sys_serial_read(sys_serial_shm_data* data, sys_serial_event_type* etype, char msg[SYS_SERIAL_SHM_DATA_SIZE])
{
    if (data->head == data->tail)
    {
        fprintf(stderr, "sys_serial_read: failed, there is nothing to read\n");
        return false;
    }

    const uint32_t head = data->head;
    const uint32_t tail = data->tail;
    const uint8_t firstbyte = data->buffer[tail];

    switch (firstbyte)
    {
    case sys_serial_event_type_led:
    case sys_serial_event_type_name:
    case sys_serial_event_type_value:
    case sys_serial_event_type_unit:
        break;
    default:
        fprintf(stderr, "sys_serial_read: failed, invalid byte %02x\n", firstbyte);
        data->tail = tail + 1;
        return false;
    }

    // keep reading until reaching null byte or head
    uint32_t i, nexttail = tail + 1;
    for (i=0; i < SYS_SERIAL_SHM_DATA_SIZE; ++i, ++nexttail)
    {
        if (nexttail == SYS_SERIAL_SHM_DATA_SIZE)
            nexttail = 0;

        msg[i] = data->buffer[nexttail];

        if (msg[i] == 0)
            break;

        if (nexttail == head)
        {
            i = SYS_SERIAL_SHM_DATA_SIZE;
            break;
        }
    }

    if (i == SYS_SERIAL_SHM_DATA_SIZE)
    {
        fprintf(stderr, "sys_serial_read: failed, tail reached head without finding null byte\n");
        data->tail = head;
        return false;
    }

    *etype = firstbyte;
    data->tail = nexttail + 1;
    return true;
}

// client, not thread-safe, needs write lock
static inline
bool sys_serial_write(sys_serial_shm_data* data, sys_serial_event_type etype, const char* msg)
{
    uint32_t size = strlen(msg);

    if (size == 0)
    {
        fprintf(stderr, "sys_serial_write: failed, empty message\n");
        return false;
    }
    if (size >= SYS_SERIAL_SHM_DATA_SIZE)
    {
        fprintf(stderr, "sys_serial_write: failed, message too big\n");
        return false;
    }

    // add space for etype and terminating null byte
    size += 2;

    const uint32_t head = data->head;
    const uint32_t tail = data->tail;
    const uint32_t wrap = tail > head ? 0 : SYS_SERIAL_SHM_DATA_SIZE;

    if (size >= wrap + tail - head)
    {
        fprintf(stderr, "sys_serial_write: failed, not enough space\n");
        return false;
    }

    data->buffer[head] = etype;

    uint32_t nexthead = head + size;

    if (nexthead > SYS_SERIAL_SHM_DATA_SIZE)
    {
        nexthead -= SYS_SERIAL_SHM_DATA_SIZE;

        const uint32_t firstpart = SYS_SERIAL_SHM_DATA_SIZE - head - 1;

        if (firstpart != 0)
            memcpy(data->buffer + head + 1, msg, firstpart);

        memcpy(data->buffer, msg + firstpart, nexthead - 1);
    }
    else
    {
        if (nexthead == SYS_SERIAL_SHM_DATA_SIZE)
            nexthead = 0;

        memcpy(data->buffer + head + 1, msg, size - 1);
    }

    data->head = nexthead;
    sem_post(&data->sem);
    return true;
}
