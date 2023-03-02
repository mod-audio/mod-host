/* Based on glibc memset, tweaked to insert memory-barrier calls

   Copyright (C) 1991-2023 Free Software Foundation, Inc.
   This file is part of the GNU C Library.

   The GNU C Library is free software; you can redistribute it and/or
   modify it under the terms of the GNU Lesser General Public
   License as published by the Free Software Foundation; either
   version 2.1 of the License, or (at your option) any later version.
   The GNU C Library is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   Lesser General Public License for more details.
   You should have received a copy of the GNU Lesser General Public
   License along with the GNU C Library; if not, see
   <https://www.gnu.org/licenses/>.
*/

#pragma once

#if defined(__MOD_DEVICES__) && defined(_MOD_DEVICE_DWARF)

#include <stdlib.h>
#include <string.h>

/* Type to use for aligned memory operations.
   This should normally be the biggest type supported by a single load
   and store.  */
#define	op_t	unsigned long int
#define OPSIZ	(sizeof (op_t))

/* Type to use for unaligned operations.  */
typedef unsigned char byte;

#define MEMSET_WAIT_OP asm volatile("dsb sy" : : : "memory");
// #define MEMSET_WAIT_OP asm volatile("dmb ish" : : : "memory");
// #define MEMSET_WAIT_OP

__attribute__((optimize(0)))
static inline
void* mod_memset(void* dstpp, int c, size_t len)
{
    long int dstp = (long int) dstpp;

    if (len >= 8)
    {
        size_t xlen;
        op_t cccc;

        cccc = (unsigned char) c;
        cccc |= cccc << 8;
        cccc |= cccc << 16;

        if (OPSIZ > 4)
            /* Do the shift in two steps to avoid warning if long has 32 bits.  */
            cccc |= (cccc << 16) << 16;

        /* There are at least some bytes to set.
           No need to test for LEN == 0 in this alignment loop.  */
        while (dstp % OPSIZ != 0)
        {
            ((byte *) dstp)[0] = c;
            MEMSET_WAIT_OP
            dstp += 1;
            len -= 1;
        }

        /* Write 8 `op_t' per iteration until less than 8 `op_t' remain.  */
        xlen = len / (OPSIZ * 8);
        while (xlen > 0)
        {
            ((op_t *) dstp)[0] = cccc;
            MEMSET_WAIT_OP
            ((op_t *) dstp)[1] = cccc;
            MEMSET_WAIT_OP
            ((op_t *) dstp)[2] = cccc;
            MEMSET_WAIT_OP
            ((op_t *) dstp)[3] = cccc;
            MEMSET_WAIT_OP
            ((op_t *) dstp)[4] = cccc;
            MEMSET_WAIT_OP
            ((op_t *) dstp)[5] = cccc;
            MEMSET_WAIT_OP
            ((op_t *) dstp)[6] = cccc;
            MEMSET_WAIT_OP
            ((op_t *) dstp)[7] = cccc;
            MEMSET_WAIT_OP
            dstp += 8 * OPSIZ;
            xlen -= 1;
        }
        len %= OPSIZ * 8;

        /* Write 1 `op_t' per iteration until less than OPSIZ bytes remain.  */
        xlen = len / OPSIZ;
        while (xlen > 0)
        {
            ((op_t *) dstp)[0] = cccc;
            MEMSET_WAIT_OP
            dstp += OPSIZ;
            xlen -= 1;
        }
        len %= OPSIZ;
    }

    /* Write the last few bytes.  */
    while (len > 0)
    {
        ((byte *) dstp)[0] = c;
        MEMSET_WAIT_OP
        dstp += 1;
        len -= 1;
    }

    MEMSET_WAIT_OP
    return dstpp;
}

static inline
void* mod_calloc(size_t nmemb, size_t size)
{
    size *= nmemb;
    void* dstpp = malloc(size);
    if (dstpp != NULL)
        mod_memset(dstpp, 0, size);
    return dstpp;
}

#else

#define mod_memset memset
#define mod_calloc calloc

#endif // _MOD_DEVICE_DWARF || STANDALONE_TEST
