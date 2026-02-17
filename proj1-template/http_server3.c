
/*
 * CS 1652 Project 1
 * (c) Jack Lange, 2020
 * (c) <Student names here>
 *
 * Computer Science Department
 * University of Pittsburgh
 */


#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>

#include <fcntl.h>

#include <sys/stat.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include "pet_list.h"
#include "pet_hashtable.h"


#define FILENAMESIZE 100
#define BUFSIZE      1024


/* Global connection tracking structure */
struct list_head connection_list = LIST_HEAD_INIT(connection_list);


typedef enum 
{
	STATE_READING_REQUEST,
	STATE_READING_FILE,
	STATE_WRITING_RESPONSE,
} conn_state_t;


struct connection {
	int       sock;
	int       fd;

	conn_state_t  state;

	char req_buf[BUFSIZE];
	int req_len;

	char *resp_buf;
	int resp_len;
	int resp_sent;

	long file_size;
	char *file_buf;
	int file_read;

	struct list_head list_node;
};


static void
free_connection(struct connection * con)
{
	if (con->fd >= 0) 
    {
		close(con->fd);
	}
	close(con->sock);
	if (con->resp_buf) 
    {
		free(con->resp_buf);
	}
	if (con->file_buf) 
    {
		free(con->file_buf);
	}
	list_del(&con->list_node);
	free(con);
}


/*
 * You are not required to use this function, but can use it or modify it as you see fit 
 */
static void 
send_response(struct connection * con) 
{
	char * ok_response_f  = "HTTP/1.0 200 OK\r\n"     					\
	    				    "Content-type: text/plain\r\n"              \
	    				    "Content-length: %ld \r\n\r\n";
	
	char * notok_response = "HTTP/1.0 404 FILE NOT FOUND\r\n"  			\
	    					"Content-type: text/html\r\n"               \
	    					"Content-length: %d\r\n\r\n"               \
	    					"<html><body bgColor=black text=white>\n"   \
	    					"<h2>404 FILE NOT FOUND</h2>\n"             \
	    					"</body></html>\n";

	char error_body[]  = "<html><body bgColor=black text=white>\n"
	                     "<h2>404 FILE NOT FOUND</h2>\n"
	                     "</body></html>\n";
	int  error_body_len = strlen(error_body);

	char header_buf[BUFSIZE];
	int  header_len;

	/* send headers */
	if (con->file_buf != NULL) 
    {
		header_len = snprintf(header_buf, BUFSIZE, ok_response_f, con->file_size);

		con->resp_len  = header_len + con->file_size;
		con->resp_buf  = malloc(con->resp_len);
		memcpy(con->resp_buf, header_buf, header_len);
		memcpy(con->resp_buf + header_len, con->file_buf, con->file_size);
	} 
    else 
    {
		header_len = snprintf(header_buf, BUFSIZE, notok_response, error_body_len);

		con->resp_len = header_len + error_body_len;
		con->resp_buf = malloc(con->resp_len);
		memcpy(con->resp_buf, header_buf, header_len);
		memcpy(con->resp_buf + header_len, error_body, error_body_len);
	}

	con->resp_sent = 0;
	con->state     = STATE_WRITING_RESPONSE;

	/* send response */
	while (con->resp_sent < con->resp_len) 
    {
		int written = write(con->sock, con->resp_buf + con->resp_sent, con->resp_len  - con->resp_sent);
		if (written < 0) 
        {
			if (errno == EAGAIN) 
            {
				break;
			}
			break;
		}
		con->resp_sent += written;
	}
}

/*
 * You are not required to use this function, but can use it or modify it as you see fit 
 */
static void 
handle_file_data(struct connection * con) 
{
	/* Read available file data */
	while (con->file_read < con->file_size) 
    {
		int bytes = read(con->fd, con->file_buf + con->file_read, con->file_size - con->file_read);
		if (bytes < 0) 
        {
			if (errno == EAGAIN) 
            {
				return;
			}
			free_connection(con);
			return;
		}
		if (bytes == 0) 
        {
			break;
		}
		con->file_read += bytes;
	}

	/* Check if we have read entire file */
	if (con->file_read < con->file_size) 
    {
		return;
	}

	/* If we have read the entire file, send response to client */
	close(con->fd);
	con->fd = -1;
	send_response(con);
}


/*
 * You are not required to use this function, but can use it or modify it as you see fit 
 */
static void 
handle_request(struct connection * con)
{
	char filename[FILENAMESIZE];
	struct stat st;

	/* parse request to get file name */
	memset(filename, 0, FILENAMESIZE);
	if (sscanf(con->req_buf, "GET %s", filename) != 1) 
    {
		send_response(con);
		return;
	}

	/* Assumption: For this project you only need to handle GET requests and filenames that contain no spaces */
	if (filename[0] == '/') 
    {
		memmove(filename, filename + 1, strlen(filename));
	}

	/* get file  size */
	if (stat(filename, &st) < 0) 
    {
		con->file_buf  = NULL;
		con->file_size = 0;
		send_response(con);
		return;
	}
	con->file_size = st.st_size;
	con->file_buf  = malloc(con->file_size);
	con->file_read = 0;

	/* try opening the file */
	con->fd = open(filename, O_RDONLY);
	if (con->fd < 0) 
    {
		free(con->file_buf);
		con->file_buf  = NULL;
		con->file_size = 0;
		send_response(con);
		return;
	}
    
	/* set to non-blocking */
	fcntl(con->fd, F_SETFL, O_NONBLOCK);

	/* Initiate non-blocking file read operations */
	con->state = STATE_READING_FILE;
	handle_file_data(con);
}

/*
 * You are not required to use this function, but can use it or modify it as you see fit 
 */
static void 
handle_network_data(struct connection * con) 
{
	/* Read all available request data */
	while (con->req_len < BUFSIZE - 1) 
    {
		int bytes = read(con->sock, con->req_buf + con->req_len, BUFSIZE - 1 - con->req_len);
		if (bytes < 0) 
        {
			if (errno == EAGAIN) 
            {
				break;
			}
			free_connection(con);
			return;
		}
		if (bytes == 0) 
        {
			break;
		}
		con->req_len += bytes;

		/* Check if we have received all the headers */
		if (con->req_len >= 4 && con->req_buf[con->req_len - 4] == '\r' &&
	        con->req_buf[con->req_len - 3] == '\n' && con->req_buf[con->req_len - 2] == '\r' &&
			con->req_buf[con->req_len - 1] == '\n') 
        {
			break;
		}
	}

	/* If we have all the headers check if we have the entire request */
	if (con->req_len < 4) 
    {
		return;
	}
	if (con->req_buf[con->req_len - 4] != '\r' || con->req_buf[con->req_len - 3] != '\n' ||
		con->req_buf[con->req_len - 2] != '\r' || con->req_buf[con->req_len - 1] != '\n') 
    {
		return;
	}

	/* If we have the entire request, then handle the request */
	handle_request(con);
}


int
main(int argc, char ** argv) 
{
	int server_port = -1;
	int listen_sock = -1;
	int opt = 1;
	struct sockaddr_in server_addr;
	struct connection * con;
	struct connection * tmp;

	/* parse command line args */
	if (argc != 2) 
    {
		fprintf(stderr, "usage: http_server3 port\n");
		exit(-1);
	}

	server_port = atoi(argv[1]);

	if (server_port < 1500) 
    {
		fprintf(stderr, "INVALID PORT NUMBER: %d; can't be < 1500\n", server_port);
		exit(-1);
	}
    
	/* Initialize connection tracking data structure */
	INIT_LIST_HEAD(&connection_list);

	/* initialize and make server socket */
	listen_sock = socket(AF_INET, SOCK_STREAM, 0);
	if (listen_sock < 0) 
    {
		perror("socket");
		exit(-1);
	}

	setsockopt(listen_sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
	fcntl(listen_sock, F_SETFL, O_NONBLOCK);

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

	/* set up for connection handling loop */
	while (1) 
    {
		fd_set read_fds;
		fd_set write_fds;
		int max_fd = listen_sock;

		/* create read and write lists */
		FD_ZERO(&read_fds);
		FD_ZERO(&write_fds);

		FD_SET(listen_sock, &read_fds);

		list_for_each_entry(con, &connection_list, list_node) 
        {
			if (con->state == STATE_READING_REQUEST) 
            {
				FD_SET(con->sock, &read_fds);
				if (con->sock > max_fd) 
                    max_fd = con->sock;
			} 
            else if (con->state == STATE_READING_FILE) 
            {
				FD_SET(con->fd, &read_fds);
				if (con->fd > max_fd) 
                    max_fd = con->fd;
			} 
            else if (con->state == STATE_WRITING_RESPONSE) 
            {
				FD_SET(con->sock, &write_fds);
				if (con->sock > max_fd) 
                    max_fd = con->sock;
			}
		}

		/* do a select */
		if (select(max_fd + 1, &read_fds, &write_fds, NULL, NULL) < 0) 
        {
			perror("select");
			continue;
		}

		/* process socket descriptors that are ready */
		if (FD_ISSET(listen_sock, &read_fds)) 
        {
			int new_sock = accept(listen_sock, NULL, NULL);
			if (new_sock >= 0) 
            {
				fcntl(new_sock, F_SETFL, O_NONBLOCK);

				con = calloc(1, sizeof(struct connection));
				con->sock     = new_sock;
				con->fd       = -1;
				con->state    = STATE_READING_REQUEST;
				con->req_len  = 0;
				con->resp_buf = NULL;
				con->file_buf = NULL;
				INIT_LIST_HEAD(&con->list_node);
				list_add_tail(&con->list_node, &connection_list);
			}
		}

		list_for_each_entry_safe(con, tmp, &connection_list, list_node) 
        {
			if (con->state == STATE_READING_REQUEST && FD_ISSET(con->sock, &read_fds)) 
            {
				handle_network_data(con);
			}
		}

		/* process file descriptors that are ready */
		list_for_each_entry_safe(con, tmp, &connection_list, list_node) 
        {
			if (con->state == STATE_READING_FILE && con->fd >= 0 && FD_ISSET(con->fd, &read_fds)) 
            {
				handle_file_data(con);
			}
		}

		list_for_each_entry_safe(con, tmp, &connection_list, list_node) 
        {
			if (con->state == STATE_WRITING_RESPONSE && FD_ISSET(con->sock, &write_fds)) 
                {
				while (con->resp_sent < con->resp_len) 
                {
					int written = write(con->sock, con->resp_buf + con->resp_sent,
					        con->resp_len  - con->resp_sent);
					if (written < 0) 
                    {
						if (errno == EAGAIN) 
                            break;
						break;
					}
					con->resp_sent += written;
				}
				if (con->resp_sent >= con->resp_len) 
                {
					free_connection(con);
				}
			}
		}
	}
}