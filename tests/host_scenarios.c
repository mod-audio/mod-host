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
*
************************************************************************************************************************
*/

/* Runs tests/host-scenarios.txt against a host on a socket:
 *   host_scenarios <port> <scenario file> NAME=value ... */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "../src/host-scenario.h"

int main(int argc, char **argv)
{
    host_scenario_var_t vars[HOST_SCENARIO_MAX_VARS];
    host_scenario_list_t list;
    int var_count = 0;
    int fd, failures, lines, i;

    if (argc < 3)
    {
        fprintf(stderr, "usage: %s <port> <scenario file> [NAME=value ...]\n", argv[0]);
        return 2;
    }
    for (i = 3; i < argc && var_count < HOST_SCENARIO_MAX_VARS; i++)
    {
        char *eq = strchr(argv[i], '=');
        if (!eq)
        {
            fprintf(stderr, "not NAME=value: %s\n", argv[i]);
            return 2;
        }
        *eq = '\0';
        vars[var_count].name = argv[i];
        vars[var_count].value = eq + 1;
        var_count++;
    }

    if (host_scenario_load(argv[2], vars, var_count, &list) < 0)
    {
        fprintf(stderr, "cannot read %s\n", argv[2]);
        return 2;
    }
    fd = host_scenario_socket_open("127.0.0.1", atoi(argv[1]));
    if (fd < 0)
    {
        fprintf(stderr, "cannot connect to port %s\n", argv[1]);
        host_scenario_free(&list);
        return 2;
    }

    failures = host_scenario_run(&list, host_scenario_socket_exchange, &fd, NULL);
    lines = list.count;
    close(fd);
    host_scenario_free(&list);

    if (failures < 0)
    {
        printf("host scenarios: the host went away\n");
        return 1;
    }
    printf("host scenarios: %d lines, %d failed\n", lines, failures);
    return failures == 0 ? 0 : 1;
}
