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

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <readline/readline.h>
#include <readline/history.h>

#include "completer.h"
#include "effects.h"
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

static const char *g_commands[] = {
    "add",
    "remove",
    "preset_load",
    "preset_save",
    "preset_show",
    "connect",
    "disconnect",
    "bypass",
    "param_set",
    "param_get",
    "param_monitor",
    "licensee",
    "monitor",
    "monitor_output",
    "midi_learn",
    "midi_map",
    "midi_unmap",
    "midi_program_listen",
#define CC_MAP              "cc_map %i %s %i %i %s %f %f %f %i %s %i ..."
#define CC_UNMAP            "cc_unmap %i %s"
    "cpu_load",
    "load",
    "save",
    "bundle_add",
    "bundle_remove",
    "feature_enable",
    "transport",
    "output_data_ready",
    "help",
    "quit",
    NULL
};

static const char *g_condition[] = {
    ">",
    ">=",
    "<",
    "<=",
    "==",
    "!=",
    NULL
};

static const char *g_features[] = {
    "link",
    "processing",
    NULL
};


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

#define UNUSED_PARAM(var)       do { (void)(var); } while (0)


/*
************************************************************************************************************************
*           LOCAL GLOBAL VARIABLES
************************************************************************************************************************
*/

static char **g_plugins_list, **g_ports_list, **g_instances_list;
static const char **g_list, **g_scale_points, **g_symbols;
static float **g_param_range;

/*
************************************************************************************************************************
*           LOCAL FUNCTION PROTOTYPES
************************************************************************************************************************
*/

static char *dupstr(const char *s);
static uint32_t spaces_count(const char *str);
static char **completion(const char *text, int start, int end);
static char *generator(const char *text, int state);
static void update_ports_list(const char *flow);
static void update_instances_list(void);


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

static char *dupstr(const char *s)
{
    char *r;

    r = malloc(strlen(s) + 1);

    if (!r)
    {
        fprintf(stderr, "malloc error\n");
        exit(EXIT_FAILURE);
    }

    strcpy(r, s);
    return (r);
}

static uint32_t spaces_count(const char *str)
{
    const char *pstr = str;
    uint8_t quote = 0;
    uint32_t count = 0;

    while (*pstr)
    {
        if (*pstr == ' ' && quote == 0)
        {
            count++;
        }

        if (*pstr == '"')
        {
            if (quote == 0) quote = 1;
            else
            {
                if (*(pstr+1) == '"') pstr++;
                else quote = 0;
            }
        }

        pstr++;
    }

    return count;
}


// ignore char** to const char** pointer type switching for this function
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wincompatible-pointer-types"

/* Attempt to complete on the contents of TEXT.  START and END bound the
   region of rl_line_buffer that contains the word to complete.  TEXT is
   the word to complete.  We can use the entire contents of rl_line_buffer
   in case we want to do some simple parsing.  Return the array of matches,
   or NULL if there aren't any.*/
static char **completion(const char *text, int start, int end)
{
    UNUSED_PARAM(end);

    char **matches;
    matches = (char **)NULL;

    /* If this word is at the start of the line, then it is a command
       to complete.  Otherwise it is the name of a file in the current
       directory. */
    if (start == 0)
    {
        g_list = g_commands;
    }
    else
    {
        g_list = NULL;

        char *line, **cmd;
        line = str_duplicate(rl_line_buffer);
        cmd = strarr_split(line);
        uint32_t count = spaces_count(rl_line_buffer);

        uint8_t get_instances = 0, get_symbols = 0, get_symbols_output = 0, get_param_info = 0, get_presets = 0;

        if (count > 0)
        {
            if (strcmp(cmd[0], "add") == 0)
            {
                if (count == 1)
                {
                    g_list = g_plugins_list;
                }
            }
            else if ((strcmp(cmd[0], "remove") == 0) ||
                     (strcmp(cmd[0], "bypass") == 0) ||
                     (strcmp(cmd[0], "licensee") == 0))
            {
                if (count == 1)
                {
                    get_instances = 1;
                }
            }
            else if ((strcmp(cmd[0], "connect") == 0) ||
                     (strcmp(cmd[0], "disconnect") == 0))
            {
                if (count == 1)
                {
                    update_ports_list("output");
                    g_list = g_ports_list;
                }
                else if (count == 2)
                {
                    update_ports_list("input");
                    g_list = g_ports_list;
                }
            }
            else if (strcmp(cmd[0], "preset_load") == 0 ||
                     strcmp(cmd[0], "preset_show") == 0)
            {
                if (count == 1)
                {
                    get_instances = 1;
                }
                else if (count == 2)
                {
                    get_presets = 1;
                }
            }
            else if (strcmp(cmd[0], "preset_save") == 0)
            {
                if (count == 1)
                {
                    get_instances = 1;
                }
            }
            else if (strcmp(cmd[0], "param_get") == 0 ||
                     strcmp(cmd[0], "midi_unmap") == 0 ||
                     strcmp(cmd[0], "cc_unmap") == 0)
            {
                if (count == 1)
                {
                    get_instances = 1;
                }
                else if (count == 2)
                {
                    get_symbols = 1;
                }
            }
            else if (strcmp(cmd[0], "param_set") == 0)
            {
                if (count == 1)
                {
                    get_instances = 1;
                }
                else if (count == 2)
                {
                    get_symbols = 1;
                }
                else if (count == 3)
                {
                    get_param_info = 1;
                }
            }
            else if (strcmp(cmd[0], "midi_learn") == 0)
            {
                if (count == 1)
                {
                    get_instances = 1;
                }
                else if (count == 2)
                {
                    get_symbols = 1;
                }
                else if (count == 3 || count == 4)
                {
                    get_param_info = 1;
                }
            }
            else if (strcmp(cmd[0], "cc_map") == 0)
            {
                if (count == 1)
                {
                    get_instances = 1;
                }
                else if (count == 2)
                {
                    get_symbols = 1;
                }
                else if (count == 6)
                {
                    get_param_info = 1;
                }
            }
            else if (strcmp(cmd[0], "param_monitor") == 0)
            {
                if (count == 1)
                {
                    get_instances = 1;
                }
                else if (count == 2)
                {
                    get_symbols = 1;
                }
                else if (count == 3)
                {
                    g_list = g_condition;
                }
                else if (count == 4)
                {
                    get_param_info = 1;
                }
            }
            else if (strcmp(cmd[0], "monitor_output") == 0)
            {
                if (count == 1)
                {
                    get_instances = 1;
                }
                else if (count == 2)
                {
                    get_symbols = 1;
                    get_symbols_output = 1;
                }
            }
            else if (strcmp(cmd[0], "feature_enable") == 0)
            {
                if (count == 1)
                {
                    g_list = g_features;
                }
            }

            if (get_instances)
            {
                update_instances_list();
                g_list = g_instances_list;
            }
            if (get_presets)
            {
                effects_get_presets_uris(atoi(cmd[1]), g_symbols);
                g_list = g_symbols;
            }
            if (get_symbols)
            {
                effects_get_parameter_symbols(atoi(cmd[1]), get_symbols_output, g_symbols);
                g_list = g_symbols;
            }
            if (get_param_info)
            {
                effects_get_parameter_info(atoi(cmd[1]), cmd[2], g_param_range, g_scale_points);

                printf("\ndef: %.03f, min: %.03f, max: %.03f, curr: %.03f\n",
                       *g_param_range[0], *g_param_range[1], *g_param_range[2], *g_param_range[3]);

                if (g_scale_points[0])
                {
                    uint32_t i;
                    printf("scale points:\n");
                    for (i = 0; g_scale_points[i]; i+=2)
                    {
                        printf("   %s: %s\n", g_scale_points[i], g_scale_points[i+1]);
                    }
                }

                rl_on_new_line();
            }

            FREE(cmd);
            FREE(line);
        }
    }

    if (!g_list) rl_bind_key('\t', rl_abort);

    matches = rl_completion_matches(text, generator);

    return (matches);
}

// back to normal
#pragma GCC diagnostic pop


/* Generator function for command completion.  STATE lets us know whether
   to start from scratch; without any state (i.e. STATE == 0), then we
   start at the top of the list. */
static char *generator(const char *text, int state)
{
    static int list_index, len;
    const char *name;

    /* If this is a new word to complete, initialize now.  This includes
       saving the length of TEXT for efficiency, and initializing the index
       variable to 0. */
    if (!state)
    {
        list_index = 0;
        len = strlen(text);
    }

    if (g_list == NULL) return ((char *)NULL);

    /* Return the next name which partially matches from the command list. */
    while ((name = g_list[list_index]) != NULL)
    {
        list_index++;

        if (strncmp(name, text, len) == 0)
            return (dupstr(name));
    }

    /* If no names matched, then return NULL. */
    return ((char *)NULL);
}


static void update_ports_list(const char *flow)
{
    FILE *fp;
    char buffer[1024];
    unsigned int ports_count = 0, i;

    /* Gets the amount of ports */
    fp = popen("jack_lsp | wc -l", "r");
    if (fp)
    {
        if (fgets(buffer, sizeof(buffer), fp))
            ports_count = atoi(buffer);

        pclose(fp);
    }

    /* Free the memory */
    if (g_ports_list)
    {
        i = 0;
        while (g_ports_list[i])
        {
            free(g_ports_list[i]);
            i++;
        }

        free(g_ports_list);
        g_ports_list = NULL;
    }

    /* Gets ports list */
    char cmd[64];
    sprintf(cmd, "jack_lsp -p | grep -B1 %s | grep -v 'properties.*,$' | grep -v ^--", flow);
    fp = popen(cmd, "r");
    if (fp)
    {
        g_ports_list = (char **) calloc(ports_count + 1, sizeof(char *));

        i = 0;
        while (fgets(buffer, sizeof(buffer), fp) != NULL)
        {
            g_ports_list[i] = (char *) calloc(1, strlen(buffer));
            memcpy(g_ports_list[i], buffer, strlen(buffer) - 1);
            i++;
        }

        pclose(fp);
    }
}


static void update_instances_list(void)
{
    FILE *fp;
    char buffer[1024];
    unsigned int instances_count = 0, i;
    static unsigned int last_instances_count = 0;

    /* Gets the amount of ports */
    fp = popen("jack_lsp | grep effect_ | wc -l", "r");
    if (fp)
    {
        if (fgets(buffer, sizeof(buffer), fp))
            instances_count = atoi(buffer);

        pclose(fp);
    }

    if (last_instances_count == instances_count) return;
    last_instances_count = instances_count;

    /* Free the memory */
    if (g_instances_list)
    {
        i = 0;
        while (g_instances_list[i])
        {
            free(g_instances_list[i]);
            i++;
        }

        free(g_instances_list);
        g_instances_list = NULL;
    }

    /* Gets ports list */
    fp = popen("jack_lsp | grep effect_", "r");
    if (fp)
    {
        g_instances_list = (char **) calloc(instances_count + 1, sizeof(char *));

        i = 0;
        int start = 0;
        while (fgets(buffer, sizeof(buffer), fp) != NULL)
        {
            unsigned int j;
            for (j = 0; j < strlen(buffer); j++)
            {
                if (buffer[j] == '_') start = j + 1;
                else if (buffer[j] == ':')
                {
                    buffer[j] = 0;
                    break;
                }
            }
            g_instances_list[i] = (char *) calloc(1, 5);
            strcpy(g_instances_list[i], &buffer[start]);
            i++;
        }

        pclose(fp);
    }
}


/*
************************************************************************************************************************
*           GLOBAL FUNCTIONS
************************************************************************************************************************
*/

void completer_init(void)
{
    rl_attempted_completion_function = completion;

    FILE *fp;
    char buffer[1024];
    unsigned int plugins_count = 0, i = 0;

    /* Gets the amount of plugins */
    fp = popen("lv2ls | wc -l", "r");
    if (fp)
    {
        if (fgets(buffer, sizeof(buffer), fp))
            plugins_count = atoi(buffer);

        pclose(fp);
    }

    /* Gets the list of plugins */
    g_plugins_list = (char **) calloc(plugins_count + 1, sizeof(char *));
    fp = popen("lv2ls", "r");
    if (fp)
    {
        while (fgets(buffer, sizeof(buffer), fp) != NULL)
        {
            g_plugins_list[i] = (char *) calloc(1, strlen(buffer));
            memcpy(g_plugins_list[i], buffer, strlen(buffer) - 1);
            i++;
        }

        pclose(fp);
    }

    g_ports_list = NULL;
    g_instances_list = NULL;

    /* Create a array of strings to symbols and scale points */
    g_symbols = (const char **) calloc(128, sizeof(char *));
    g_scale_points = (const char **) calloc(256, sizeof(char *));

    /* Allocates memory to parameter range */
    g_param_range = (float **) calloc(4, sizeof(float *));
    g_param_range[0] = (float *) malloc(sizeof(float));
    g_param_range[1] = (float *) malloc(sizeof(float));
    g_param_range[2] = (float *) malloc(sizeof(float));
    g_param_range[3] = (float *) malloc(sizeof(float));
}
