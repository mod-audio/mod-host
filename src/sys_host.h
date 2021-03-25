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
#include <sys/mman.h>
#include <unistd.h>

typedef struct {
    sem_t sem;
    uint8_t buffer[4096 - sizeof(sem_t)];
} sys_serial_shm_data;

static inline
bool sys_serial_open(int* shmfd, sys_serial_shm_data** data, bool server)
{
    int fd;
    sem_t sem;
    sys_serial_shm_data* ptr;

    fd = server
       ? shm_open(SYS_SERIAL_SHM, O_CREAT|O_EXCL|O_RDWR, 0600)
       : shm_open(SYS_SERIAL_SHM, O_RDWR, 0);

    if (fd < 0)
    {
        fprintf(stderr, "shm_open failed\n");
        return false;
    }

    if (server)
    {
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
    }

    ptr = mmap(NULL, sizeof(sys_serial_shm_data), PROT_READ|PROT_WRITE, MAP_SHARED|MAP_LOCKED, fd, 0);

    if (ptr == NULL || ptr == MAP_FAILED)
    {
        fprintf(stderr, "mmap failed\n");
        goto cleanup_sem;
    }

    ptr->sem = sem;
    memset(ptr->buffer, 0, sizeof(ptr->buffer));

    *shmfd = fd;
    *data = ptr;
    return true;

cleanup_sem:
    /*
    if (server)
        sem_destroy(sem);
    */

cleanup:
    close(fd);

    if (server)
        shm_unlink(SYS_SERIAL_SHM);

    *shmfd = 0;
    *data = NULL;
    return false;
}

static inline
void sys_serial_close(int shmfd, sys_serial_shm_data* data, bool server)
{
    if (server)
        sem_destroy(&data->sem);

    munmap(data, sizeof(sys_serial_shm_data));
    close(shmfd);

    if (server)
        shm_unlink(SYS_SERIAL_SHM);
}
