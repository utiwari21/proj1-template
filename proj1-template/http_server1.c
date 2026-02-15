/*
 * CS 1652 Project 1
 * (c) Jack Lange, 2020
 * (c) Amy Babay, 2022
 * (c) Utkarsh Tiwari and Justin Li
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

#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/socket.h>


#define BUFSIZE 1024
#define FILENAMESIZE 100


static int
handle_connection(int sock)
{

    char * ok_response_f  = "HTTP/1.0 200 OK\r\n"        \
        				    "Content-type: text/plain\r\n"                  \
        					"Content-length: %d \r\n\r\n";
 
    char * notok_response = "HTTP/1.0 404 FILE NOT FOUND\r\n"   \
        					"Content-type: text/html\r\n\r\n"                       \
        					"<html><body bgColor=black text=white>\n"               \
        					"<h2>404 FILE NOT FOUND</h2>\n"
        					"</body></html>\n";
    
	//(void)notok_response;  // DELETE ME
	//(void)ok_response_f;   // DELETE ME

    /* first read loop -- get request and headers*/
    //read until blank line: \r\n\r\n the first sequence to end header, second for new line
    char buf[BUFSIZE];
    size_t bytes_read = 0;
    int total_bytes = 0;

    while(1) {

        bytes_read = read(sock, buf + total_bytes, BUFSIZE - total_bytes);

        if (bytes_read < 0) {
            fprintf(stderr, "%s", notok_response);
            return -1;
        }

        if (bytes_read == 0) {
            // EOF
            break;
        }

        total_bytes += bytes_read;

        // Check for end of headers
        if (strstr(buf, "\r\n\r\n") != NULL || strstr(buf, "\n\n") != NULL) {
            break;
        }
    }
    
    /* parse request to get file name */
    /* Assumption: this is a GET request and filename contains no spaces*/

    //GET /filename HTTP/1.0\r\n\r\n
    //reading from a string so sscanf

    char filename[FILENAMESIZE];
    sscanf(buf, "GET /%s HTTP/1.0", filename);

    if(strlen(filename) == 0)
    {
        //no filename specified
        fprintf(stderr, "%s", notok_response); //logging purposes
        //error to http_client
        write(sock, notok_response, strlen(notok_response));
        close(sock);
        return -1;
    }

    /* open and read the file */
    FILE *file = fopen(filename, "r");
    if (file == NULL) {
        // send not ok response
        fprintf(stderr, "%s", notok_response);
        write(sock, notok_response, strlen(notok_response));
        close(sock);
        return -1;
    }
	
	/* send response */
    char file_buf[BUFSIZE];
    int file_len = 0;
    //get file size
    fseek(file, 0L, SEEK_END); // Move pointer to the end of the file
    long int size = ftell(file); // Get current position (file size)

    //bring back to beginning of the file
    fseek(file, 0L, SEEK_SET);

    //sending header
    char header[BUFSIZE];
    int header_len = snprintf(header, BUFSIZE, ok_response_f, (int)size);
    write(sock, header, header_len);

    //sending file content
    while (1) {
        int bytes_read = fread(file_buf, 1, BUFSIZE, file);
        if (bytes_read <= 0) {
            break;
        }
        file_len += bytes_read;
        write(sock, file_buf, bytes_read);
    }

    /* close socket and free pointers */
    fclose(file);
    close(sock);
	return 0;
}


int
main(int argc, char ** argv)
{
    int server_port = -1;
    int ret         =  0;
    int sock        = -1;

    /* parse command line args */
    if (argc != 2) {
        fprintf(stderr, "usage: http_server1 port\n");
        exit(-1);
    }

    server_port = atoi(argv[1]);

    if (server_port < 1500) {
        fprintf(stderr, "INVALID PORT NUMBER: %d; can't be < 1500\n", server_port);
        exit(-1);
    }

    /* initialize and make socket */
    sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (sock < 0) {
        fprintf(stderr, "Error creating socket\n");
        exit(-1);
    }

    /* set server address */
    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(server_port);
    server_addr.sin_addr.s_addr = INADDR_ANY;

    /* bind listening socket */
    if (bind(sock, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        fprintf(stderr, "Error binding socket\n");
        close(sock);
        exit(-1);
    }
    /* start listening */
    if (listen(sock, 10) < 0) {
        fprintf(stderr, "Error listening on socket\n");
        close(sock);
        exit(-1);
    }

    /* connection handling loop: wait to accept connection */
    while (1) {
        int client_sock = accept(sock, NULL, NULL);
        if(client_sock < 0) {
            fprintf(stderr, "Error accepting connection\n");
            continue;
        }
        /* handle connections */
        ret = handle_connection(client_sock);
		//(void)ret; // DELETE ME
    }
}
