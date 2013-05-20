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

static char *g_commands[] = {
  "add",
  "remove",
  "connect",
  "disconnect",
  "bypass",
  "param_set",
  "param_get",
  "help",
  "quit",
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

static char **g_list, **g_plugins_list, **g_ports_list, **g_symbols, **g_instances_list;


/*
************************************************************************************************************************
*           LOCAL FUNCTION PROTOTYPES
************************************************************************************************************************
*/

static char *dupstr(char *s);
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

static char *dupstr(char *s)
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

        str_array_t cmd;
        cmd = string_split(rl_line_buffer, ' ');
        if (cmd.count > 0)
        {
            if (strcmp(cmd.data[0], "add") == 0)
            {
                if (cmd.count == 2) g_list = g_plugins_list;
            }
            else if ((strcmp(cmd.data[0], "remove") == 0) ||
                     (strcmp(cmd.data[0], "bypass") == 0))
            {
                if (cmd.count == 2)
                {
                    update_instances_list();
                    g_list = g_instances_list;
                }
            }
            else if ((strcmp(cmd.data[0], "connect") == 0) ||
                     (strcmp(cmd.data[0], "disconnect") == 0))
            {
                if (cmd.count == 2)
                {
                    update_ports_list("output");
                    g_list = g_ports_list;
                }
                else if (cmd.count == 3)
                {
                    update_ports_list("input");
                    g_list = g_ports_list;
                }
            }
            else if ((strcmp(cmd.data[0], "param_set") == 0) ||
                     (strcmp(cmd.data[0], "param_get") == 0))
            {
                if (cmd.count == 2)
                {
                    update_instances_list();
                    g_list = g_instances_list;
                }
                else if (cmd.count == 3)
                {
                    effects_get_controls_symbols(atoi(cmd.data[1]), g_symbols);
                    g_list = g_symbols;
                }
            }

            free_str_array(cmd);
        }
    }

    if (!g_list) rl_bind_key('\t', rl_abort);

    matches = rl_completion_matches(text, generator);

    return (matches);
}


/* Generator function for command completion.  STATE lets us know whether
   to start from scratch; without any state (i.e. STATE == 0), then we
   start at the top of the list. */
static char *generator(const char *text, int state)
{
    static int list_index, len;
    char *name;

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
    unsigned int ports_count, i;

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
    unsigned int instances_count, i;
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
    unsigned int plugins_count, i = 0;

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

    /* Create a array of strings */
    g_symbols = (char **) calloc(128, sizeof(char *));
}
