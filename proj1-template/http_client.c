/*
 * CS 1652 Project 1 
 * (c) Jack Lange, 2020
 * (c) Amy Babay, 2022
 * (c) Utkarsh Tiwari & Justin Li 
 * 
 * Computer Science Department
 * University of Pittsburgh
 */


#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include <netdb.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/socket.h>

#define BUFSIZE 1024

int
main(int argc, char ** argv)
{

    char * server_name = NULL;
    int    server_port = -1;
    char * server_path = NULL;
    char * req_str     = NULL;

    int ret = 0;

    /*parse args */
    if (argc != 4) {
        fprintf(stderr, "usage: http_client <hostname> <port> <path>\n");
        exit(-1);
    }

    server_name = argv[1];
    server_port = atoi(argv[2]);
    server_path = argv[3];
    
    /* Create HTTP request */
    ret = asprintf(&req_str, "GET %s HTTP/1.0\r\n\r\n", server_path);
    if (ret == -1) {
        fprintf(stderr, "Failed to allocate request string\n");
        exit(-1);
    }

    /*
     * NULL accesses to avoid compiler warnings about unused variables
     * You should delete the following lines
     */
    //(void)server_name;
    //(void)server_port;

    /* make socket */
    int sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    //from server1 main method
    if(sock < 0) {
        fprintf(stderr, "Error creating socket\n");
        exit(-1);
    }

    /* get host IP address  */
    /* Hint: use gethostbyname() */
    struct hostent * hp = gethostbyname(server_name);
    if(hp == NULL) {
        fprintf(stderr, "Error resolving hostname\n");
        exit(-1);
    }

    /* set address */
    struct sockaddr_in saddr;
    memset(&saddr, 0, sizeof(saddr));
    saddr.sin_family = AF_INET;
    memcpy(&saddr.sin_addr.s_addr, hp->h_addr, hp->h_length);
    saddr.sin_port = htons(server_port);

    /* connect to the server */
    if (connect(sock, (struct sockaddr *)&saddr, sizeof(saddr)) < 0) {
        fprintf(stderr, "Error connecting to server\n");
        close(sock);
        exit(-1);
    }

    /* send request message */
    //send request message
    size_t total_sent = 0;
    while(total_sent < strlen(req_str)) {
        ssize_t sent = write(sock, req_str + total_sent, strlen(req_str) - total_sent);
        if (sent < 0) {
            fprintf(stderr, "Error sending request\n");
            close(sock);
            exit(-1);
        }
        total_sent += sent;
    }

    /* wait for response (i.e. wait until socket can be read) */
    /* Hint: use select(), and ignore timeout for now. */
    fd_set read_fds;
    FD_ZERO(&read_fds);
    FD_SET(sock, &read_fds);

    int select_ret = select(sock + 1, &read_fds, NULL, NULL, NULL);
    if(select_ret < 0) {
        fprintf(stderr, "Error in select\n");
        close(sock);
        exit(-1);
    }

    /* first read loop -- read headers */
    char response[BUFSIZE];
    int total_bytes = 0;
    ssize_t bytes_read = 0;
    
    while(1)
    {
        bytes_read = read(sock, response + total_bytes, BUFSIZE - total_bytes );
        if(bytes_read < 0) {
            fprintf(stderr, "Error reading response\n");
            close(sock);
            exit(-1);
        }

        if(bytes_read == 0) {
            break; // EOF
        }

        total_bytes += bytes_read;

        //check for end of headers
        if(strstr(response, "\r\n\r\n") != NULL) {
            break;
        }
    }

    /* examine return code */
    // Skip protocol version (e.g. "HTTP/1.0")
    // Normal reply has return code 200

    int return_code = 0;
    sscanf(response, "HTTP/%*s %d", &return_code);

    // Find where headers end
    char *body_start = strstr(response, "\r\n\r\n");
    int header_end_len = 4;

    if (body_start == NULL) {
        // No header terminator found, all data is headers
        body_start = response + total_bytes;
    } else {
        body_start += header_end_len;  // Skip past the terminator
    }

    int body_size_so_far = response + total_bytes - body_start;

    /* print first part of response: header, error code, etc. */

    //success
    if(return_code == 200) {
        //print body
        printf("%.*s", body_size_so_far, body_start);
    } else {
        //print header and body so far
        fprintf(stderr, "%.*s", total_bytes, response);
    }
    

    /* second read loop -- print out the rest of the response: real web content */
    char buf[BUFSIZE];

    while(1) {
        bytes_read = read(sock, buf, BUFSIZE);
        if(bytes_read < 0) {
            fprintf(stderr, "Error reading response body\n");
            close(sock);
            exit(-1);
        }

        if(bytes_read == 0) {
            break; // EOF
        }

        if (return_code == 200) {
            // Success: print to stdout
            printf("%.*s", (int)bytes_read, buf);
        } else {
            // Error: print to stderr
            fprintf(stderr, "%.*s", (int)bytes_read, buf);
        }
    }

    /* close socket */
    close(sock);
    if(return_code != 200) {
        exit(-1);
    }
    exit(0);
}
