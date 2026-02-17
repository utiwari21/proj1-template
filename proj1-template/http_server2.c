/*
 * CS 1652 Project 1 
 * (c) Jack Lange, 2020
 * (c) Amy Babay, 2022
 * (c) <Student names here>
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
#include <sys/select.h>

#define BUFSIZE 1024
#define FILENAMESIZE 100
#define MAX_CONNECTIONS 64


typedef struct 
{
	int sock;
	bool active;
} connection_t;


static int 
handle_connection(int sock) 
{
	char buffer[BUFSIZE];
	char filename[FILENAMESIZE];
	char request[BUFSIZE];

	int bytes_read;
	int total_read = 0;
    
	FILE *file;
	long file_size;
	char *file_buffer;
	size_t result;
	char response_header[BUFSIZE];
	int header_len;

	char * ok_response_f  = "HTTP/1.0 200 OK\r\n"						\
							"Content-type: text/plain\r\n"				\
							"Content-length: %ld \r\n\r\n";
	
	char * notok_response = "HTTP/1.0 404 FILE NOT FOUND\r\n"			\
							"Content-type: text/html\r\n"				\
							"Content-length: %d\r\n\r\n"				\
							"<html><body bgColor=black text=white>\n"	\
							"<h2>404 FILE NOT FOUND</h2>\n"				\
							"</body></html>\n";

	/* first read loop -- get request and headers*/
	memset(request, 0, BUFSIZE);
	while (total_read < BUFSIZE - 1) 
    {
		bytes_read = read(sock, buffer, 1);
		if (bytes_read <= 0) 
        {
			break;
		}

		request[total_read] = buffer[0];
		total_read++;
		if (total_read >= 4 && 
			request[total_read - 4] == '\r' && 
			request[total_read - 3] == '\n' &&
			request[total_read - 2] == '\r' && 
			request[total_read - 1] == '\n') 
            {
			break;
		    }
	}

	/* parse request to get file name */
	memset(filename, 0, FILENAMESIZE);
	if (sscanf(request, "GET %s", filename) != 1) 
    {
		close(sock);
		return -1;
	}

	/* Assumption: For this project you only need to handle GET requests and filenames that contain no spaces */
	if (filename[0] == '/') 
    {
		memmove(filename, filename + 1, strlen(filename));
	}

	/* open and read the file */
	file = fopen(filename, "rb");
	if (file == NULL) 
    {
		/* send response */
		char error_body[] = "<html><body bgColor=black text=white>\n"
							"<h2>404 FILE NOT FOUND</h2>\n"
							"</body></html>\n";
		int error_len = strlen(error_body);
		snprintf(response_header, BUFSIZE, notok_response, error_len);
		write(sock, response_header, strlen(response_header));
		write(sock, error_body, error_len);
		
		/* close socket and free space */
		close(sock);
		return -1;
	}

	fseek(file, 0, SEEK_END);
	file_size = ftell(file);
	rewind(file);

	file_buffer = (char *)malloc(sizeof(char) * file_size);
	if (file_buffer == NULL) 
    {
		fclose(file);
		close(sock);
		return -1;
	}

	result = fread(file_buffer, 1, file_size, file);
	if (result != (size_t)file_size) 
    {
		free(file_buffer);
		fclose(file);
		close(sock);
		return -1;
	}

	/* send response */
	header_len = snprintf(response_header, BUFSIZE, ok_response_f, file_size);
	write(sock, response_header, header_len);
	write(sock, file_buffer, file_size);

	/* close socket and free space */
	free(file_buffer);
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
	int listen_sock = -1;
	struct sockaddr_in server_addr;
	connection_t connections[MAX_CONNECTIONS];
	int i;
	fd_set read_fds;
	int max_fd;

	/* parse command line args */
	if (argc != 2) 
    {
		fprintf(stderr, "usage: http_server2 port\n");
		exit(-1);
	}

	server_port = atoi(argv[1]);

	if (server_port < 1500) 
    {
		fprintf(stderr, "Requested port(%d) must be above 1500\n", server_port);
		exit(-1);
	}
	
	/* initialize and make socket */
	listen_sock = socket(AF_INET, SOCK_STREAM, 0);
	if (listen_sock < 0) 
    {
		perror("socket");
		exit(-1);
	}

	int opt = 1;
	if (setsockopt(listen_sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) 
    {
		perror("setsockopt");
		exit(-1);
	}

	/* set server address */
	memset(&server_addr, 0, sizeof(server_addr));
	server_addr.sin_family = AF_INET;
	server_addr.sin_addr.s_addr = INADDR_ANY;
	server_addr.sin_port = htons(server_port);

	/* bind listening socket */
	if (bind(listen_sock, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) 
    {
		perror("bind");
		exit(-1);
	}

	/* start listening */
	if (listen(listen_sock, 10) < 0) 
    {
		perror("listen");
		exit(-1);
	}

	/* initialize connection list */
	for (i = 0; i < MAX_CONNECTIONS; i++) 
    {
		connections[i].sock = -1;
		connections[i].active = false;
	}

	/* connection handling loop: wait to accept connection */

	while (1) 
    {
	
		/* create read list */
		FD_ZERO(&read_fds);
		FD_SET(listen_sock, &read_fds);
		max_fd = listen_sock;

		for (i = 0; i < MAX_CONNECTIONS; i++) 
        {
			if (connections[i].active) 
            {
				FD_SET(connections[i].sock, &read_fds);
				if (connections[i].sock > max_fd) 
                {
					max_fd = connections[i].sock;
				}
			}
		}
		
		/* do a select() */
		ret = select(max_fd + 1, &read_fds, NULL, NULL, NULL);
		if (ret < 0) 
        {
			perror("select");
			continue;
		}
		
		/* process sockets that are ready:
		 *     for the accept socket, add accepted connection to connections
		 *     for a connection socket, handle the connection
		 */
		
		if (FD_ISSET(listen_sock, &read_fds)) 
        {
			sock = accept(listen_sock, NULL, NULL);
			if (sock < 0) 
            {
				perror("accept");
				continue;
			}

			for (i = 0; i < MAX_CONNECTIONS; i++) 
            {
				if (!connections[i].active) 
                {
					connections[i].sock = sock;
					connections[i].active = true;
					break;
				}
			}

			if (i == MAX_CONNECTIONS) 
            {
				close(sock);
			}
		}

		for (i = 0; i < MAX_CONNECTIONS; i++) 
        {
			if (connections[i].active && FD_ISSET(connections[i].sock, &read_fds)) 
            {
				ret = handle_connection(connections[i].sock);
				connections[i].active = false;
				connections[i].sock = -1;
			}
		}
	}
}