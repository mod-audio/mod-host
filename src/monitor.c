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
#include <fcntl.h>

#ifdef _WIN32
#include <winsock2.h>
#else
#include <netdb.h>
#include <netinet/in.h>
#include <sys/socket.h>
#define closesocket close
#define INVALID_SOCKET -1
typedef int SOCKET;
#endif

#include "monitor.h"
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

#define OFF 0
#define ON 1

/*
************************************************************************************************************************
*           LOCAL GLOBAL VARIABLES
************************************************************************************************************************
*/

static int g_status;
static SOCKET g_sockfd;

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

int monitor_start(char *addr, int port)
{
    /* connects to the address specified by the client and starts
     * monitoring and sending information according to the settings
     * for each monitoring plugin */

    struct sockaddr_in serv_addr;
    struct hostent *server;

    g_sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (g_sockfd == INVALID_SOCKET)
    {
        perror("ERROR opening socket");
        return 1;
    }

    server = gethostbyname(addr);

    if (server == NULL)
    {
        fprintf(stderr,"ERROR, no such host");
        return 1;
    }

    memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    memcpy(server->h_addr, &serv_addr.sin_addr.s_addr, server->h_length);
    serv_addr.sin_port = htons(port);

    if (connect(g_sockfd,(struct sockaddr*)&serv_addr, sizeof(serv_addr)) < 0)
    {
        perror("ERROR connecting");
        return 1;
    }

    g_status = ON;

#ifdef _WIN32
    unsigned long mode = 1;
    if (ioctlsocket(g_sockfd, FIONBIO, &mode) != 0)
#else
    int flags = fcntl(g_sockfd, F_GETFL, 0);
    if (fcntl(g_sockfd, F_SETFL, flags | O_NONBLOCK) != 0)
#endif
    {
        perror("ERROR setting socket to nonblocking");
        return 1;
    }

    return 0;
}


int monitor_status(void)
{
    return g_status;
}

int monitor_stop(void)
{
    closesocket(g_sockfd);
    g_status = OFF;
    return 0;
}

int monitor_send(int instance, const char *symbol, float value)
{
    int size, ret = -1;

    char msg[255];
    char *buffer = msg;
    size = sprintf(msg, "monitor %d %s %f", instance, symbol, value);

    while (size > 0)
    {
        ret = send(g_sockfd, buffer, size + 1, 0);
        if (ret < 0)
        {
            perror("send error");
        }
        size -= ret;
        buffer += ret;
    }

    return ret;
}


int monitor_check_condition(int op, float cond_value, float value)
{
    switch(op)
    {
        case 0:
            return value > cond_value ? 1 : 0;
        case 1:
            return value >= cond_value ? 1 : 0;
        case 2:
            return value < cond_value ? 1 : 0;
        case 3:
            return value <= cond_value ? 1 : 0;
        case 4:
            return floats_differ_enough(value, cond_value) ? 0 : 1;
        case 5:
            return floats_differ_enough(value, cond_value) ? 1 : 0;
    }
    return 0;
}

