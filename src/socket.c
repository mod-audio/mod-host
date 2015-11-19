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

#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>

#include "socket.h"


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


/*
************************************************************************************************************************
*           LOCAL GLOBAL VARIABLES
************************************************************************************************************************
*/

static int g_serverfd, g_buffer_size;
static void (*g_receive_cb)(msg_t *msg);


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


/*
************************************************************************************************************************
*           GLOBAL FUNCTIONS
************************************************************************************************************************
*/

int socket_start(int port, int buffer_size)
{
    g_serverfd = socket(AF_INET, SOCK_STREAM, 0);
    if (g_serverfd < 0)
    {
        perror("socket error");
        return -1;
    }

    /* Allow the reuse of the socket address */
    int value = 1;
    setsockopt(g_serverfd, SOL_SOCKET, SO_REUSEADDR, &value, sizeof(value));

    /* Startup the socket struct */
    struct sockaddr_in serv_addr;
    memset((char *) &serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = INADDR_ANY;
    serv_addr.sin_port = htons(port);

    /* Try assign the address */
    if (bind(g_serverfd, (struct sockaddr *) &serv_addr, sizeof(serv_addr)) < 0)
    {
        perror("bind error");
        return -1;
    }

    /* Start listen the socket */
    if (listen(g_serverfd, 0) < 0)
    {
        perror("listen error");
        return -1;
    }

    g_receive_cb = NULL;
    g_buffer_size = buffer_size;

    return 0;
}


void socket_finish(void)
{
    shutdown(g_serverfd, SHUT_RDWR);
    close(g_serverfd);
}


void socket_set_receive_cb(void (*receive_cb)(msg_t *msg))
{
    g_receive_cb = receive_cb;
}


int socket_send(int destination, const char *buffer, int size)
{
    int ret;

    ret = write(destination, buffer, size);
    if (ret < 0)
    {
        perror("send error");
    }

    return ret;
}


void socket_run(int exit_on_failure)
{
    int clientfd, count;
    struct sockaddr_in cli_addr;
    socklen_t clilen;
    char *buffer;
    msg_t msg;

    /* Allocates memory to receive buffer */
    buffer = malloc(g_buffer_size);
    if (buffer == NULL)
    {
        if (! exit_on_failure)
            return;

        perror("malloc error");
        exit(EXIT_FAILURE);
    }

    /* Wait for client connection */
    clilen = sizeof(cli_addr);
    clientfd = accept(g_serverfd, (struct sockaddr *) &cli_addr, &clilen);
    if (clientfd < 0)
    {
        free(buffer);

        if (! exit_on_failure)
            return;

        perror("accept error");
        exit(EXIT_FAILURE);
    }

    while (1)
    {
        memset(buffer, 0, g_buffer_size);
        count = read(clientfd, buffer, g_buffer_size);

        if (count > 0) /* Data received */
        {
            msg.sender_id = clientfd;
            msg.data = buffer;
            msg.data_size = count;
            if (g_receive_cb) g_receive_cb(&msg);
        }
        else if (count < 0) /* Error */
        {
            if (! exit_on_failure)
                break;

            perror("read error");
            exit(EXIT_FAILURE);
        }
        else /* Client disconnected */
        {
            break;
        }
    }

    free(buffer);

    close(clientfd);
}
