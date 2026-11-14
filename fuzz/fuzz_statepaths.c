/* The state directory of an instance: MakePluginStatePath() and GetPluginStateDir() with any instance and
   any request path, below a scratch directory. Properties: the directory answered is exactly the scratch
   directory and the effect directory, and the path answered is exactly that and the request ("." names the
   effect directory itself). */
#include "fuzz.h"
#include "../src/state-paths.h"
#include <stdio.h>
#include <sys/stat.h>

static const char g_root[] = "fuzz-statepaths";

int LLVMFuzzerInitialize(int *argc, char ***argv)
{
    (void)argc; (void)argv;
    mkdir(g_root, 0755);
    return 0;
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    if (size < 5 || size > 256)
        return 0;

    int instance;
    memcpy(&instance, data, sizeof(instance));
    char *request = fuzz_message(data + 4, size - 4);

    /* confined to what the verb can reach: the request is a path under the instance, never one that climbs out */
    if (strstr(request, "..") == NULL)
    {
        char *path = MakePluginStatePath(instance, g_root, request);
        /* A request below the scratch directory is made: every directory of it is created or is there, since no
           part of a request of at most 252 bytes is longer than a file name may be.
           mutant: src/state-paths.c:209 `mkdir(duppath, 0755) && errno != EEXIST` -> `1`
           input: nested */
        if (!path)
            abort();
        char *want = malloc(strlen(g_root) + strlen(request) + 32);
        /* not a property: the harness's own allocation failed */
        if (!want)
            abort();
        if (strcmp(request, ".") == 0)
            sprintf(want, "%s/effect-%d", g_root, instance);
        else
            sprintf(want, "%s/effect-%d/%s", g_root, instance, request);
        /* The path is the scratch directory, the effect directory and the request, nothing else.
           mutant: src/state-paths.c:175 `*buf++ = '/';` -> `;`
           input: nested */
        if (strcmp(path, want) != 0)
            abort();
        char *last = strcmp(request, ".") == 0 ? NULL : strrchr(path, '/');
        struct stat st;
        if (last)
            *last = '\0';
        /* The directories of the path exist: all of it for the effect directory itself ("."), and up to its last "/"
           for a file below it.
           mutant: src/state-paths.c:209 `mkdir(duppath, 0755) && errno != EEXIST` -> `0`
           input: nested */
        if (stat(path, &st) != 0 || !S_ISDIR(st.st_mode))
            abort();
        free(want);
        free(path);
    }

    char want_dir[96];
    snprintf(want_dir, sizeof(want_dir), "%s/effect-%d", g_root, instance);
    char *dir = GetPluginStateDir(instance, g_root);
    /* The effect directory of an instance is the scratch directory and "effect-<instance>".
       mutant: src/state-paths.c:128 `"effect-%d"` -> `"effect+%d"`
       input: nested */
    if (!dir || strcmp(dir, want_dir) != 0)
        abort();
    free(dir);
    free(request);

    RecursivelyRemovePluginPath(g_root);
    mkdir(g_root, 0755);
    return 0;
}
