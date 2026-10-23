/*
 * Checks that strarr_split() stays inside the string it splits, whatever it holds.
 *
 * Each string is copied into an allocation of exactly its size, so AddressSanitizer reports any byte read or
 * written outside it. Build and run with "make utils-run" in this directory.
 */

#include "../src/utils.c"

#include <stdio.h>

static int g_failures;

static void check(const char *input, const char *first)
{
    size_t size = strlen(input) + 1;
    char *str = malloc(size);
    char **list;

    memcpy(str, input, size);
    list = strarr_split(str);

    if (list != NULL && strcmp(list[0], first) == 0)
    {
        printf("ok      \"%s\"\n", input);
    }
    else
    {
        printf("FAILED  \"%s\": expected first token \"%s\", got \"%s\"\n", input, first, list ? list[0] : "(null)");
        g_failures++;
    }

    free(list);
    free(str);
}

int main(void)
{
    /* line by line, so the results are out before a sanitizer report ends the process */
    setvbuf(stdout, NULL, _IOLBF, 0);

    check("cpu_load", "cpu_load");
    check("cpu_load   ", "cpu_load");
    check("param_get 0 gain", "param_get");
    check("", "");
    check("   ", "");
    check(" \t\r\n", "");

    if (g_failures)
    {
        printf("%d check(s) failed\n", g_failures);
        return EXIT_FAILURE;
    }

    printf("all checks passed\n");
    return EXIT_SUCCESS;
}
