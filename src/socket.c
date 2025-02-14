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

#ifdef _WIN32
#include <winsock2.h>
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#define closesocket close
#define INVALID_SOCKET -1
typedef int SOCKET;
#endif

#include "socket.h"
#include "effects.h"
#include "mod-memset.h"


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

static SOCKET g_serverfd = INVALID_SOCKET;
static SOCKET g_fbserverfd = INVALID_SOCKET;
static SOCKET g_clientfd = INVALID_SOCKET;
static SOCKET g_fbclientfd = INVALID_SOCKET;

static int g_buffer_size;
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

int socket_start(int socket_port, int feedback_port, int buffer_size)
{
#ifdef _WIN32
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0)
    {
        perror("WSAStartup");
        return -1;
    }

    if (LOBYTE(wsaData.wVersion) != 2 || HIBYTE(wsaData.wVersion) != 2)
    {
        WSACleanup();
        perror("WSAStartup  version");
        return -1;
    }
#endif

    g_clientfd = g_fbclientfd = INVALID_SOCKET;
    g_serverfd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);

    if (g_serverfd == INVALID_SOCKET)
    {
        perror("g_serverfd socket error");
        return -1;
    }

    if (feedback_port != 0)
    {
        g_fbserverfd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);

        if (g_fbserverfd == INVALID_SOCKET)
        {
            perror("g_fbserverfd socket error");
            return -1;
        }
    }
    else
    {
        g_fbserverfd = INVALID_SOCKET;
    }

#ifndef _WIN32
    /* Allow the reuse of the socket address */
    int value = 1;
    setsockopt(g_serverfd, SOL_SOCKET, SO_REUSEPORT, &value, sizeof(value));
    if (feedback_port != 0)
        setsockopt(g_fbserverfd, SOL_SOCKET, SO_REUSEPORT, &value, sizeof(value));

    /* Increase socket size */
    value = 131071;
    setsockopt(g_serverfd, SOL_SOCKET, SO_RCVBUF, &value, sizeof(value));
    if (feedback_port != 0)
        setsockopt(g_fbserverfd, SOL_SOCKET, SO_RCVBUF, &value, sizeof(value));

    /* Set TCP_NODELAY */
    setsockopt(g_serverfd, IPPROTO_TCP, TCP_NODELAY, &value, sizeof(value));
    if (feedback_port != 0)
        setsockopt(g_fbserverfd, IPPROTO_TCP, TCP_NODELAY, &value, sizeof(value));
#endif

    /* Startup the socket struct */
    struct sockaddr_in serv_addr;
    memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
#if defined(__MOD_DEVICES__) || defined(_WIN32)
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
    if (listen(g_serverfd, -1) < 0)
    {
        perror("listen error");
        return -1;
    }

    if (feedback_port != 0 && listen(g_fbserverfd, -1) < 0)
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
    if (g_serverfd == INVALID_SOCKET)
        return;

    // make local copies so that we can invalidate these vars first
    const SOCKET serverfd   = g_serverfd;
    const SOCKET fbserverfd = g_fbserverfd;
    g_serverfd = g_fbserverfd = INVALID_SOCKET;

    // shutdown clients, but don't close them
    if (g_fbclientfd != INVALID_SOCKET)
        shutdown(g_fbclientfd, SHUT_RDWR);
    shutdown(g_clientfd, SHUT_RDWR);

    // shutdown and close servers
    if (fbserverfd != INVALID_SOCKET)
    {
        shutdown(fbserverfd, SHUT_RDWR);
        closesocket(fbserverfd);
    }

    shutdown(serverfd, SHUT_RDWR);
    closesocket(serverfd);

#ifdef _WIN32
    WSACleanup();
#endif
}


void socket_set_receive_cb(void (*receive_cb)(msg_t *msg))
{
    g_receive_cb = receive_cb;
}


int socket_send(int destination, const char *buffer, int size)
{
    int ret = -1;

    while (size > 0)
    {
        ret = send(destination, buffer, size, 0);
        if (ret < 0)
        {
            perror("send error");
        }
        size -= ret;
        buffer += ret;
    }

    return ret;
}


int socket_send_feedback(const char *buffer)
{
    if (g_fbclientfd == INVALID_SOCKET) return -1;

    return socket_send(g_fbclientfd, buffer, strlen(buffer)+1);
}


void socket_run(int exit_on_failure)
{
    SOCKET clientfd, fbclientfd;
    int count;
    char *buffer;
    char *msgbuffer;
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
    clientfd = accept(g_serverfd, NULL, NULL);
    if (clientfd == INVALID_SOCKET)
    {
        free(buffer);

        if (! exit_on_failure)
            return;

        perror("accept error");
        exit(EXIT_FAILURE);
    }

    if (g_fbserverfd != INVALID_SOCKET)
    {
        fbclientfd = accept(g_fbserverfd, NULL, NULL);
        if (fbclientfd == INVALID_SOCKET)
        {
            free(buffer);
            closesocket(clientfd);

            if (! exit_on_failure)
                return;

            perror("accept error");
            exit(EXIT_FAILURE);
        }
    }
    else
    {
        fbclientfd = INVALID_SOCKET;
    }

    g_clientfd = clientfd;
    g_fbclientfd = fbclientfd;

    while (g_serverfd != INVALID_SOCKET)
    {
        mod_memset(buffer, 0, g_buffer_size);
        count = recv(clientfd, buffer, g_buffer_size, 0);

        if (count > 0) /* Data received */
        {
            if (count == g_buffer_size && buffer[count - 1] != '\0')
            {
                /* if message is bigger than our buffer, dynamically allocate more data until we receive it all */
                int new_count, new_buffer_size;

                msgbuffer = NULL;
                new_count = g_buffer_size;
                new_buffer_size = g_buffer_size;

                while (new_count == g_buffer_size)
                {
                    new_buffer_size += g_buffer_size;

                    if (msgbuffer == NULL)
                    {
                        msgbuffer = malloc(new_buffer_size);
                        memcpy(msgbuffer, buffer, g_buffer_size);
                    }
                    else
                    {
                        msgbuffer = realloc(msgbuffer, new_buffer_size);
                    }

                    mod_memset(msgbuffer + count, 0, g_buffer_size);
                    new_count = recv(clientfd, msgbuffer + count, g_buffer_size, 0);

                    if (new_count > 0) /* Data received */
                    {
                        count += new_count;

                        if (msgbuffer[count - 1] == '\0')
                            break;
                    }
                    else if (new_count < 0) /* Error */
                    {
                        if (! exit_on_failure)
                            goto outside_loop;

                        perror("read error");
                        exit(EXIT_FAILURE);
                    }
                    else if (new_count == 0) /* Client disconnected */
                    {
                        goto outside_loop;
                    }
                }
            }
            else
            {
                msgbuffer = buffer;
            }

            if (g_receive_cb)
            {
                // ignore leftover null bytes
                while (count > 1 && msgbuffer[count - 1] == '\0')
                    --count;

                // make sure to keep 1 null byte at the end of the string
                ++count;

                msg.data = msgbuffer;

                while (count > 0)
                {
                    if (*msg.data == '\0')
                    {
                        --count;
                        ++msg.data;
                        continue;
                    }

                    msg.sender_id = clientfd;
                    msg.data_size = strlen(msg.data) + 1;
                    g_receive_cb(&msg);

                    count -= msg.data_size;
                    msg.data += msg.data_size;
                }
            }

            if (msgbuffer != buffer)
                free(msgbuffer);

            effects_idle_external_uis();
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

outside_loop:
    if (fbclientfd != INVALID_SOCKET)
    {
        g_fbclientfd = INVALID_SOCKET;
        closesocket(fbclientfd);
    }

    g_clientfd = INVALID_SOCKET;
    closesocket(clientfd);

    free(buffer);
}
