
#define MOD_SEMAPHORE_USE_FUTEX

#ifdef MOD_SEMAPHORE_USE_FUTEX
#include <linux/futex.h>
#include <sys/time.h>
#include <errno.h>
#include <syscall.h>
#include <unistd.h>
#else
#include <semaphore.h>
#endif

#ifdef MOD_SEMAPHORE_USE_FUTEX
/* --------------------------------------------------------------------- */
// Linux futex

typedef struct _sem_t {
    int value, pshared;
} sem_t;

static inline
void sem_init(sem_t* sem, int pshared, int value)
{
    sem->value   = value;
    sem->pshared = pshared;
}

static inline
void sem_destroy(sem_t* sem)
{
    // unused
    return; (void)sem;
}

static inline
void sem_post(sem_t* sem)
{
    if (! __sync_bool_compare_and_swap(&sem->value, 0, 1)) {
        // already unlocked, do not wake futex
        return;
    }

    syscall(__NR_futex, &sem->value, sem->pshared ? FUTEX_WAKE : FUTEX_WAKE_PRIVATE, 1, NULL, NULL, 0);
    return;
}

static inline
int sem_wait(sem_t* sem)
{
    for (;;)
    {
        if (__sync_bool_compare_and_swap(&sem->value, 1, 0))
            return 0;

        if (syscall(__NR_futex, &sem->value, sem->pshared ? FUTEX_WAIT : FUTEX_WAIT_PRIVATE, 0, NULL, NULL, 0) != 0 && errno != EWOULDBLOCK)
            return 1;
    }
}
// 0 = ok
static inline
int sem_timedwait_secs(sem_t* sem, int secs)
{
    const struct timespec timeout = { secs, 0 };

    for (;;)
    {
        if (__sync_bool_compare_and_swap(&sem->value, 1, 0))
            return 0;

        if (syscall(__NR_futex, &sem->value, sem->pshared ? FUTEX_WAIT : FUTEX_WAIT_PRIVATE, 0, &timeout, NULL, 0) != 0 && errno != EWOULDBLOCK)
            return 1;
    }
}
#else
/* --------------------------------------------------------------------- */
// POSIX Semaphore

static inline
int sem_timedwait_secs(sem_t* sem, int secs)
{
      struct timespec timeout;
      clock_gettime(CLOCK_REALTIME, &timeout);
      timeout.tv_sec += secs;
      return sem_timedwait(sem, &timeout);
}
#endif
