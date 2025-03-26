/*
** client.c -- a stream socket client demo
*/

// Include base C libraries
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>

// Include base C socket programming libraries
#include <netdb.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <arpa/inet.h>

// External library for JSON parser
#include <sqlite3.h>
#include "vendor/cJSON/cJSON.h"

#define PORT "7777" // the port client will be connecting to 

#define MAXDATASIZE 2048 // max number of bytes we can get at once 


// get sockaddr, IPv4 or IPv6:
void *get_in_addr(struct sockaddr *sa)
{
    if (sa->sa_family == AF_INET) {
        return &(((struct sockaddr_in*)sa)->sin_addr);
    }

    return &(((struct sockaddr_in6*)sa)->sin6_addr);
}

int main(int argc, char *argv[])
{
    int sockfd, numbytes;  
    char res[MAXDATASIZE];
    char req[MAXDATASIZE];
    struct addrinfo hints, *servinfo, *p;
    int rv;
    char s[INET6_ADDRSTRLEN];

    if (argc != 3) {
        fprintf(stderr,"usage: client hostname json_file_address\n");
        exit(1);
    }

    // Read JSON file passed through CLI
    {
        FILE *file = fopen(argv[2], "r");
        if (!file) {
            perror("Erro ao abrir arquivo");
            return -1;
        }
        size_t len = fread(req, 1, MAXDATASIZE - 1, file);
        req[len] = '\0';
        fclose(file);
    }
    

    // Configure server address
    {
        memset(&hints, 0, sizeof hints);
        hints.ai_family = AF_UNSPEC;
        hints.ai_socktype = SOCK_STREAM;
        if ((rv = getaddrinfo(argv[1], PORT, &hints, &servinfo)) != 0) {
            fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(rv));
            return 1;
        }
    }
    
    /* Connect to server
    **
    ** Loop through all the results and connect to the first we can
    */
    {
        for(p = servinfo; p != NULL; p = p->ai_next) {
            if ((sockfd = socket(p->ai_family, p->ai_socktype,
                    p->ai_protocol)) == -1) {
                perror("client: socket");
                continue;
            }
    
            if (connect(sockfd, p->ai_addr, p->ai_addrlen) == -1) {
                close(sockfd);
                perror("client: connect");
                continue;
            }
    
            break;
        }
        if (p == NULL) {
            fprintf(stderr, "client: failed to connect\n");
            return 2;
        }
        inet_ntop(p->ai_family, get_in_addr((struct sockaddr *)p->ai_addr),
                s, sizeof s);
        printf("client: connecting to %s\n", s);
        freeaddrinfo(servinfo); // all done with this structure
    }

    // Send JSON Request to Server
    {
        send(sockfd, req, strlen(req), 0);
        //Debug request:
        //printf("Client send JSON:\n%s\n", req);
    }

    // Recieve response from server
    {
        if ((numbytes = recv(sockfd, res, MAXDATASIZE-1, 0)) == -1) {
            perror("recv");
            exit(1);
        }
        res[numbytes] = '\0';
        printf("client: received '%s'\n",res);
    
        // Close server connection socket
        close(sockfd);
    }
    

    return 0;
}