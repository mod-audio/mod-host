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
*           INCLUDE FILES
************************************************************************************************************************
*/

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <errno.h>
#include <ftw.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>

#ifdef _WIN32
#include <direct.h>
#endif

#include "state-paths.h"
#include "utils.h"

/*
************************************************************************************************************************
*           LOCAL DEFINES
************************************************************************************************************************
*/


/*
************************************************************************************************************************
*           LOCAL CONSTANTS
************************************************************************************************************************
*/


/*
************************************************************************************************************************
*           LOCAL DATA TYPES
************************************************************************************************************************
*/


/*
************************************************************************************************************************
*           LOCAL MACROS
************************************************************************************************************************
*/

#define UNUSED_PARAM(var)           do { (void)(var); } while (0)


/*
************************************************************************************************************************
*           LOCAL GLOBAL VARIABLES
************************************************************************************************************************
*/


/*
************************************************************************************************************************
*           LOCAL FUNCTION PROTOTYPES
************************************************************************************************************************
*/


/*
************************************************************************************************************************
*           LOCAL CONFIGURATION ERRORS
************************************************************************************************************************
*/


/*
************************************************************************************************************************
*           LOCAL FUNCTIONS
************************************************************************************************************************
*/

static int RemovePath(const char *path, const struct stat *st, int flags, struct FTW *ftw)
{
    int rv = remove(path);

    if (rv != 0)
        perror(path);

    return rv;

    UNUSED_PARAM(st);
    UNUSED_PARAM(flags);
    UNUSED_PARAM(ftw);
}


/*
************************************************************************************************************************
*           GLOBAL FUNCTIONS
************************************************************************************************************************
*/

char* GetPluginStateDir(int instance, const char *dir)
{
    if (dir == NULL)
        return NULL;

    char effidstr[24] = {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0};
    snprintf(effidstr, 23, "effect-%d", instance);

    const size_t current_path_size = strlen(dir);
    const size_t effidstr_size     = strlen(effidstr);

    char *newpath, *buf;
    newpath = buf = malloc(current_path_size + effidstr_size + 2);

    if (newpath == NULL)
        return NULL;

    memcpy(buf, dir, current_path_size);
    buf += current_path_size;
    *buf++ = '/';
    memcpy(buf, effidstr, effidstr_size);
    buf += effidstr_size;
    *buf = '\0';

    return newpath;
}

char* MakePluginStatePath(int instance, const char *dir, const char *path)
{
    if (dir == NULL || path == NULL)
        return NULL;

    char effidstr[24] = {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0};
    snprintf(effidstr, 23, "effect-%d", instance);

    const size_t current_path_size = strlen(dir);
    const size_t effidstr_size     = strlen(effidstr);
    const size_t request_path_size = strlen(path);
    const bool   emptypath         = request_path_size == 1 && strcmp(path, ".") == 0;

    char *newpath, *buf;
    newpath = buf = malloc(current_path_size + effidstr_size + request_path_size + 3);

    if (newpath == NULL)
        return NULL;

    memcpy(buf, dir, current_path_size);
    buf += current_path_size;
    *buf++ = '/';
    memcpy(buf, effidstr, effidstr_size);
    buf += effidstr_size;
    if (! emptypath)
    {
        *buf++ = '/';
        memcpy(buf, path, request_path_size);
        buf += request_path_size;
    }
    *buf = '\0';

    if (access(newpath, F_OK) != 0)
    {
        char* duppath = str_duplicate(newpath);

        if (duppath == NULL)
        {
            free(newpath);
            return NULL;
        }

        if (! emptypath)
        {
            // create dirs up to last slash in path
            char *lastslash = strrchr(duppath, '/');
            if (lastslash != NULL)
                *lastslash = '\0';
        }

        const size_t duppath_len = strlen(duppath);

        for (size_t i = 1; i <= duppath_len; ++i)
        {
            if (duppath[i] == '/' || duppath[i] == '\0')
            {
                duppath[i] = '\0';
#ifdef _WIN32
                if (_mkdir(duppath) && errno != EEXIST)
#else
                if (mkdir(duppath, 0755) && errno != EEXIST)
#endif
                {
                    free(duppath);
                    free(newpath);
                    return NULL;
                }
                duppath[i] = '/';
            }
        }

        free(duppath);
    }

    return newpath;
}

int RecursivelyRemovePluginPath(const char *path)
{
    return nftw(path, RemovePath, 64, FTW_DEPTH | FTW_PHYS);
}
