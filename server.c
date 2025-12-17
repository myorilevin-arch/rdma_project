
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <netinet/in.h>

#define PORT 8080

int main() {
  int server_fd, client_fd;
  struct sockaddr_in address;

  server_fd = socket(AF_INET, SOCK_STREAM, 0);
  if (server_fd < 0) { perror("socket"); exit(EXIT_FAILURE); }

  int opt = 1;
  if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
    perror("setsockopt");
    exit(EXIT_FAILURE);
  }

  memset(&address, 0, sizeof(address));
  address.sin_family = AF_INET;
  address.sin_addr.s_addr = INADDR_ANY;
  address.sin_port = htons(PORT);

  if (bind(server_fd, (struct sockaddr*)&address, sizeof(address)) < 0) {
    perror("bind"); exit(EXIT_FAILURE);
  }

  //socklen_t addrlen_2 = sizeof(address);
  //if (getsockname(server_fd, (struct sockaddr*)&address, &addrlen_2) < 0) {
    //perror("getsockname");
    //exit(EXIT_FAILURE);
  //}
  //printf("Server is listening on port %d\n", ntohs(address.sin_port));

  if (listen(server_fd, 1) < 0) {
    perror("listen"); exit(EXIT_FAILURE);
  }

  printf("Waiting for client...\n");
  socklen_t addrlen = sizeof(address);
  client_fd = accept(server_fd, (struct sockaddr*)&address, &addrlen);
  if (client_fd < 0) { perror("accept"); exit(EXIT_FAILURE); }

  // Read messages from the client
  int megabyte = 1024 * 1024; // 1 MB = 1024 * 1024 bytes
  char buffer[megabyte]; // Buffer for messages
  memset(buffer, 0, sizeof(buffer));

    // Loop to receive messages from the client
    while(1){
      int n = recv(client_fd, buffer, sizeof(buffer), 0);
      if(n==0) {
        printf("Client disconnected.\n");
        break; // Client disconnected
      }
      // extract the number of messages the client is going to send from buffer
      int count = atoi(buffer);
      for(int i = 0; i < count; i++) {
        n = recv(client_fd, buffer, sizeof(buffer), 0);
        if (n <= 0) {
          printf("Error receiving message or client disconnected.\n");
          break; // Exit on error or disconnection
        }
      }
      // send a response to the client
      send(client_fd, "ACK", 3, 0);
      //printf("Sent response to client.\n");
    }

  // Close the sockets
  close(client_fd);
  close(server_fd);
  return 0;
}
