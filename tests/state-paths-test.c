/*
 * Checks that MakePluginStatePath() keeps a plugin's state paths inside its instance's directory: a request with a ".."
 * component or an absolute one is refused (NULL) and creates nothing; a request below the directory, "." for the
 * directory itself, and names that only hold dots ("a..b", "..x") are made as before.
 *
 * Build and run with "make state-paths-run" in this directory.
 */

#include "../src/state-paths.c"
#include "../src/utils.c"

#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>

static int g_failures;
static char g_root[64];

static int exists(const char *rel)
{
    char path[256];
    struct stat st;

    snprintf(path, sizeof(path), "%s/%s", g_root, rel);
    return stat(path, &st) == 0;
}

static void refused(const char *request, const char *outside)
{
    char *path = MakePluginStatePath(1, g_root, request);

    if (path == NULL && (outside == NULL || !exists(outside)))
        printf("ok      refused \"%s\"\n", request);
    else
    {
        printf("FAILED  \"%s\": expected a refusal, got \"%s\"%s\n", request, path ? path : "(null)",
               outside && exists(outside) ? ", and it made a directory outside the instance's" : "");
        g_failures++;
    }
    free(path);
}

static void made(const char *request, const char *want)
{
    char expected[256];
    char *path = MakePluginStatePath(1, g_root, request);

    snprintf(expected, sizeof(expected), "%s/%s", g_root, want);
    if (path != NULL && strcmp(path, expected) == 0)
        printf("ok      made \"%s\"\n", request);
    else
    {
        printf("FAILED  \"%s\": expected \"%s\", got \"%s\"\n", request, expected, path ? path : "(null)");
        g_failures++;
    }
    free(path);
}

int main(void)
{
    snprintf(g_root, sizeof(g_root), "/tmp/state-paths-test.%d", (int)getpid());
    if (mkdir(g_root, 0755) != 0)
    {
        perror(g_root);
        return 2;
    }

    refused("..", NULL);
    refused("../escaped", NULL);
    refused("../escaped-dir/file", "escaped-dir");
    refused("sub/../../escaped-dir2/file", "escaped-dir2");
    refused("sub/..", NULL);
    refused("./../escaped", NULL);
    refused("/etc/passwd", NULL);
    refused("/tmp/absolute-dir/file", NULL);
    refused("", NULL);

    made(".", "effect-1");
    made("file", "effect-1/file");
    made("sub/file", "effect-1/sub/file");
    made("a..b", "effect-1/a..b");
    made("..hidden", "effect-1/..hidden");
    made("sub/..x/file", "effect-1/sub/..x/file");

    RecursivelyRemovePluginPath(g_root);
    if (g_failures)
        printf("%d FAILED\n", g_failures);
    else
        printf("all passed\n");
    return g_failures ? 1 : 0;
}
