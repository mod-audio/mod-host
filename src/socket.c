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

static int g_serverfd, g_fbserverfd, g_buffer_size;
static void (*g_receive_cb)(msg_t *msg);

// socket clients, for safe external shutdown
static int g_clientfd, g_fbclientfd;

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

/* FIXME: SO_REUSEADDR with 0 'backlog' (2nd argument in listen) does not work on modern distros.
 *        We don't have time to investigate this further right now, so leave a note here for later.
 *        Using SO_REUSEPORT with -1 as 'backlog' seems to work, needs testing on MOD Duo later on.
 */
#if defined(__MOD_DEVICES__) || ! defined(SO_REUSEPORT)
#define MOD_SOCKET_FLAGS   SO_REUSEADDR
#define MOD_SOCKET_BACKLOG 0
#else
#define MOD_SOCKET_FLAGS   SO_REUSEPORT
#define MOD_SOCKET_BACKLOG -1
#endif

int socket_start(int socket_port, int feedback_port, int buffer_size)
{
    g_clientfd = g_fbclientfd = -1;
    g_serverfd = socket(AF_INET, SOCK_STREAM, 0);

    if (g_serverfd < 0)
    {
        perror("socket error");
        return -1;
    }

    if (feedback_port != 0)
    {
        g_fbserverfd = socket(AF_INET, SOCK_STREAM, 0);

        if (g_fbserverfd < 0)
        {
            perror("socket error");
            return -1;
        }
    }
    else
    {
        g_fbserverfd = -1;
    }

    /* Allow the reuse of the socket address */
    int value = 1;
    setsockopt(g_serverfd, SOL_SOCKET, MOD_SOCKET_FLAGS, &value, sizeof(value));
    if (feedback_port != 0)
        setsockopt(g_fbserverfd, SOL_SOCKET, MOD_SOCKET_FLAGS, &value, sizeof(value));

    /* increase socket size */
    value = 131071;
    setsockopt(g_serverfd, SOL_SOCKET, SO_RCVBUF, &value, sizeof(value));
    if (feedback_port != 0)
        setsockopt(g_fbserverfd, SOL_SOCKET, SO_RCVBUF, &value, sizeof(value));

    /* Startup the socket struct */
    struct sockaddr_in serv_addr;
    memset((char *) &serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
#ifdef __MOD_DEVICES__
    serv_addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
#else
    serv_addr.sin_addr.s_addr = htonl(INADDR_ANY);
#endif

    /* Try assign the server address */
    serv_addr.sin_port = htons(socket_port);
    if (bind(g_serverfd, (struct sockaddr *) &serv_addr, sizeof(serv_addr)) < 0)
    {
        perror("bind error");
        return -1;
    }

    if (feedback_port != 0)
    {
        /* Try assign the receiver address */
        serv_addr.sin_port = htons(feedback_port);
        if (bind(g_fbserverfd, (struct sockaddr *) &serv_addr, sizeof(serv_addr)) < 0)
        {
            perror("bind error");
            return -1;
        }
    }

    /* Start listen the sockets */
    if (listen(g_serverfd, MOD_SOCKET_BACKLOG) < 0)
    {
        perror("listen error");
        return -1;
    }

    if (feedback_port != 0 && listen(g_fbserverfd, MOD_SOCKET_BACKLOG) < 0)
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
    if (g_serverfd == -1)
        return;

    // make local copies so that we can invalidate these vars first
    const int serverfd   = g_serverfd;
    const int fbserverfd = g_fbserverfd;
    g_serverfd = g_fbserverfd = -1;

    // shutdown clients, but don't close them
    if (g_fbclientfd != -1)
        shutdown(g_fbclientfd, SHUT_RDWR);
    shutdown(g_clientfd, SHUT_RDWR);

    // shutdown and close servers
    if (fbserverfd != -1)
    {
        shutdown(fbserverfd, SHUT_RDWR);
        close(fbserverfd);
    }

    shutdown(serverfd, SHUT_RDWR);
    close(serverfd);
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


int socket_send_feedback(const char *buffer)
{
    if (g_fbclientfd < 0) return -1;

    return socket_send(g_fbclientfd, buffer, strlen(buffer)+1);
}


void socket_run(int exit_on_failure)
{
    int clientfd, fbclientfd, count;
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

    if (g_fbserverfd != -1)
    {
        fbclientfd = accept(g_fbserverfd, (struct sockaddr *) &cli_addr, &clilen);
        if (fbclientfd < 0)
        {
            free(buffer);
            close(clientfd);

            if (! exit_on_failure)
                return;

            perror("accept error");
            exit(EXIT_FAILURE);
        }
    }
    else
    {
        fbclientfd = -1;
    }

    g_clientfd = clientfd;
    g_fbclientfd = fbclientfd;

    while (g_serverfd >= 0)
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

    if (fbclientfd != -1)
    {
        g_fbclientfd = -1;
        close(fbclientfd);
    }

    g_clientfd = -1;
    close(clientfd);

    free(buffer);
}
