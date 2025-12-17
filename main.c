// #include <stdio.h>
// #include <stdlib.h>
// #include <string.h>
// #include <assert.h>
// #include <unistd.h>
// #include <sys/types.h>
// #include <sys/socket.h>
// #include <sys/param.h>
// #include <sys/time.h>
// #include <netdb.h>
// #include <stdbool.h>
// #include <netinet/in.h>
//
// // #include <infiniband/verbs.h>
// // #define _GNU_SOURCE
//
// #define NUM_SERVERS 4
// #define BASE_PORT 50000 // Base port number for the servers
// #define SERVER_NAME_SIZE 256
// char *SERVER_NAMES = "mlx-stud-01 mlx-stud-02 mlx-stud-03 mlx-stud-04";
//
// typedef struct pg_handle
// {
//   // struct netent *next_hostname;
//   char *next_host;
// } pg_handle;
//
// void initialize_server_socket(int port, int *listen_sock_fd, int *final_sock_fd)
// {
//   *listen_sock_fd = socket(AF_INET, SOCK_STREAM, 0);
//   if (*listen_sock_fd < 0)
//   {
//     perror("socket");
//     exit(EXIT_FAILURE);
//   }
//   // setsockopt(sockfd, level, optname, optval, socklen_t optlen); # OPTIONAL
//   struct sockaddr_in my_addr = {0};
//   my_addr.sin_family = AF_INET;
//   my_addr.sin_addr.s_addr = INADDR_ANY;
//   my_addr.sin_port = htons(port); // Port number to connect to
//   if (bind(*listen_sock_fd, (struct sockaddr *) &my_addr, sizeof(my_addr)) < 0)
//   {
//     perror("bind");
//     close(*listen_sock_fd);
//     exit(EXIT_FAILURE);
//   }
//   if (listen(*listen_sock_fd, 1) < 0)
//   {
//     perror("listen");
//     close(*listen_sock_fd);
//     exit(EXIT_FAILURE);
//   }
// }
//
// void initialize_client_socket(int port, int *send_sock_fd, const struct hostent *next_server_info)
// {
//   *send_sock_fd = socket(AF_INET, SOCK_STREAM, 0);
//   if (*send_sock_fd < 0)
//   {
//     perror("socket");
//     exit(EXIT_FAILURE);
//   }
//   struct sockaddr_in next_addr = {0};
//   next_addr.sin_family = AF_INET;
//   next_addr.sin_port = htons(port);
//   memcpy(&next_addr.sin_addr.s_addr, next_server_info->h_addr_list[0], next_server_info->h_length);
//
//   if (connect(*send_sock_fd, (struct sockaddr *) &next_addr, sizeof(next_addr)) < 0)
//   {
//     perror("connect");
//     close(*send_sock_fd);
//     exit(EXIT_FAILURE);
//   }
// }
//
// bool assign_rank_and_next_hostname(char *my_hostname, char *next_hostname, char **servers_list, int *my_rank, int *next_rank, int *prev_rank)
// {
//   if (memcmp(my_hostname, servers_list[0], strlen(servers_list[0])) == 0)
//   {
//     strncpy(next_hostname, servers_list[1], SERVER_NAME_SIZE - 1);
//     next_hostname[SERVER_NAME_SIZE - 1] = '\0';
//     *my_rank = 1;
//   }
//   else if (memcmp(my_hostname, servers_list[1], strlen(servers_list[1])) == 0)
//   {
//     strncpy(next_hostname, servers_list[2], SERVER_NAME_SIZE - 1);
//     next_hostname[SERVER_NAME_SIZE - 1] = '\0';    *my_rank = 2;
//   }
//   else if (memcmp(my_hostname, servers_list[2], strlen(servers_list[2])) == 0)
//   {
//     strncpy(next_hostname, servers_list[3], SERVER_NAME_SIZE - 1);
//     next_hostname[SERVER_NAME_SIZE - 1] = '\0';    *my_rank = 3;
//   }
//   else if (memcmp(my_hostname, servers_list[3], strlen(servers_list[3])) == 0)
//   {
//     strncpy(next_hostname, servers_list[0], SERVER_NAME_SIZE - 1);
//     next_hostname[SERVER_NAME_SIZE - 1] = '\0';    *my_rank = 4;
//   }
//   else
//   {
//     fprintf(stderr, "Unknown hostname: %s\n", my_hostname);
//     return EXIT_FAILURE;
//   }
//   printf("next_hostname middle: %s\n", next_hostname);
//
//   *next_rank = (*my_rank % NUM_SERVERS) + 1;
//   *prev_rank = (*my_rank - 1) == 0 ? NUM_SERVERS : (*my_rank - 1);
//   return EXIT_SUCCESS;
// }
//
// int connect_process_group(char *servername, void **pg)
// {
//   sock_connect(const char *servername, int port);
//
//
//   char my_hostname[SERVER_NAME_SIZE];
//   char next_hostname[SERVER_NAME_SIZE];
//
//   if (gethostname(my_hostname, sizeof(my_hostname)) < 0)
//   {
//     perror("gethostname");
//     exit(EXIT_FAILURE);
//   }
//
//   int my_rank = -1;
//   int next_rank = -1;
//   int prev_rank = -1;
//   char *servers_list[NUM_SERVERS];
//   int count = 0;
//
//   char servers_copy[NUM_SERVERS * SERVER_NAME_SIZE + NUM_SERVERS]; // Enough space for all server names and spaces
//   strncpy(servers_copy, servername, sizeof(servers_copy));
//   servers_copy[sizeof(servers_copy)-1] = '\0';
//   char *token = strtok(servers_copy, " ");
//
//
//   while (token != NULL && count < NUM_SERVERS)
//   {
//     servers_list[count++] = token;
//     token = strtok(NULL, " ");
//   }
//   printf("next_hostname before: %s\n", next_hostname);
//
//   if (assign_rank_and_next_hostname(my_hostname, next_hostname, servers_list, &my_rank, &next_rank, &prev_rank) != 0)
//   {
//     printf("Failed to assign rank\n");
//     exit(EXIT_FAILURE);
//   }
//   printf("my_hostname: %s\n", my_hostname);
//   printf("next_hostname: %s\n", next_hostname);
//
//   int listen_sock_fd, getting_sock_fd, sending_sock_fd;
//
//   initialize_server_socket(BASE_PORT + my_rank, &listen_sock_fd, &getting_sock_fd);
//
//   sleep(10); // Allow time for all processes to start listening
//
//   struct hostent *next_server_info = gethostbyname(next_hostname);
//   initialize_client_socket(BASE_PORT + next_rank, &sending_sock_fd, next_server_info);
//
//   struct sockaddr_in prev_addr = {0};
//   socklen_t prev_addr_len = sizeof(prev_addr);
//   getting_sock_fd = accept(listen_sock_fd, (struct sockaddr *) &prev_addr, &prev_addr_len);
//   if (getting_sock_fd < 0)
//   {
//     perror("accept");
//     close(listen_sock_fd);
//     close(sending_sock_fd);
//     exit(EXIT_FAILURE);
//   }
//   printf("[Rank %d] Accepted connection from previous rank %d\n", my_rank, prev_rank);
//
//   if (my_rank == 1)
//   {
//     const char *message = "Hello from Rank 1!";
//     printf("rank %d Sending message: %s\n", my_rank, message);
//     write(sending_sock_fd, message, strlen(message) + 1);
//
//     char buffer[1024] = {0};
//     read(getting_sock_fd, buffer, sizeof(buffer));
//     printf("rank %d Received message: %s\n test success!", my_rank, buffer);
//   }
//   else
//   {
//     char buffer[1024] = {0};
//     read(getting_sock_fd, buffer, sizeof(buffer));
//     printf("rank %d Received message: %s\n", my_rank, buffer);
//
//     write(sending_sock_fd, buffer, strlen(buffer) + 1);
//     printf("rank %d Forwarded message to next rank %d\n", my_rank, next_rank);
//   }
//
//   // Store the pg_handle
//   pg_handle *pg = malloc(sizeof(pg_handle));
//   if (!pg)
//   {
//     perror("malloc");
//     close(getting_sock_fd);
//     close(sending_sock_fd);
//     close(listen_sock_fd);
//     exit(EXIT_FAILURE);
//   }
//
//
//   close(getting_sock_fd);
//   close(sending_sock_fd);
//   close(listen_sock_fd);
//   return 0;
// }


#include "rdma_lib.h"
#include <stdio.h>
#include <stdlib.h>

int main(int argc, char *argv[])
{
  if (argc < 2)
  {
    fprintf(stderr, "Usage: %s <server_names>\n", argv[0]);
    return EXIT_FAILURE;
  }

  void *pg = NULL;
  connect_process_group(argv[1], &pg);
  return 0;
}
