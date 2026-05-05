/*
  Copyright 2026 Filipe Coelho <falktx@falktx.com>

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

#define _GNU_SOURCE 1

#include "memfd-dlopen.h"
#undef dlopen

#include <stdbool.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/mman.h>

void* memfd_dlopen(const char* path, int flags)
{
    FILE* const module_file = fopen(path, "rb");
    if (module_file == NULL) {
        fprintf(stderr, "Failed to open shared object %s\n", path);
        return NULL;
    }

    fseek(module_file, 0, SEEK_END);
    const long size = ftell(module_file);
    fseek(module_file, 0, SEEK_SET);

    do {
        const int shm_fd = memfd_create(path, MFD_CLOEXEC);
        if (shm_fd < 0) {
            fprintf(stderr, "Failed to create memory file for %s\n", path);
            break;
        }

        if (ftruncate(shm_fd, size) < 0) {
            fprintf(stderr, "Failed to truncate memory file for %s\n", path);
            close(shm_fd);
            break;
        }

        char buf[8192];
        bool ok = false;
        for (long i = 0; i < size;)
        {
            const int r = fread(buf, 1, 8192, module_file);
            if (r == 0) {
                ok = true;
                break;
            }
            const int w = write(shm_fd, buf, r);
            if (r != w) {
                fprintf(stderr, "Failed to write memory file for %s\n", path);
                break;
            }
        }

        if (! ok)
            break;

        snprintf(buf, 8192, "/proc/%d/fd/%d", getpid(), shm_fd);

        void* const handle = dlopen(buf, flags);
        close(shm_fd);

        if (handle == NULL) {
            fprintf(stderr, "Failed to load memory file for %s\n", path);
            break;
        }

        fclose(module_file);

        return handle;

    } while (false);

    fclose(module_file);

    return dlopen(path, flags);
}
