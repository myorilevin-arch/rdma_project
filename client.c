#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <time.h>
#include <math.h>

#define PORT 8080
#define IP_ADDRESS "10.164.164.101"
#define MESSAGES_AMOUNT 1000 // Number of messages to send in each measurement

int measure(int message_size, int sock, char* msg) {
  int total_bytes_sent = 0;
  struct timespec start, end;
  clock_gettime(CLOCK_MONOTONIC, &start);
  //printf("Sending message of size %d :\n", message_size);

  for(int i = 0; i < MESSAGES_AMOUNT; i++){
    int bytes_sent = send(sock, msg, message_size, 0);
    total_bytes_sent += bytes_sent;
  }

  //printf("Done sending message of size %d.\n", message_size);

  // Receive response from server
  char buffer[1024] = {0};
  int n = recv(sock, buffer, sizeof(buffer), 0);
  if(n==0) {
    printf("didn't get message\n");
    perror("recv");
    exit(EXIT_FAILURE);
  }
  //else if (n>0) {
    //printf("Received ACK from server\n");
  //}

  clock_gettime(CLOCK_MONOTONIC, &end);
  double time_spent = (end.tv_sec - start.tv_sec) +
                      (end.tv_nsec - start.tv_nsec) / 1e9;

  printf("size of messages sent: %d\n", message_size);
  //printf("Total bytes sent: %d\n", total_bytes_sent);
  //printf("Time taken: %.3f seconds\n", time_spent);
  printf("Throughput: %.2f KB/s\n\n", (total_bytes_sent / time_spent) / 1024.0);
  //shutdown(sock, SHUT_RDWR); // Shutdown the socket to prevent further communication
}


int main () {
  int sock = socket(AF_INET, SOCK_STREAM, 0);
  if (sock < 0) { perror("socket"); exit(EXIT_FAILURE); }

  struct sockaddr_in serv_addr;
  memset(&serv_addr, 0, sizeof(serv_addr));
  serv_addr.sin_family = AF_INET;
  serv_addr.sin_port = htons(PORT);
  inet_pton(AF_INET, IP_ADDRESS, &serv_addr.sin_addr);

  if (connect(sock, (struct sockaddr*)&serv_addr, sizeof(serv_addr)) < 0) {
    perror("connect"); exit(EXIT_FAILURE);
  }

    int warm_up_cycles = 1000;
    int throughput_per_message_size = 0;
    int msg_amount = 10000;

    int megabyte = pow(2, 20); // 1 MB = 2^20 bytes
    for (int message_size = 1; message_size <= megabyte; message_size *= 2) {

      char msg[message_size]; // Buffer for messages
      memset(msg, 0, sizeof(msg)); // Fill with 0
      send(sock, &msg_amount, sizeof(int*), 0);

      // warm up
      //printf("Sending %d warm up cycles:\n", warm_up_cycles);
      for(int i = 0; i < warm_up_cycles ; i++) {
        snprintf(msg, message_size, "WARM_UP_message%d\n", i);
        send(sock, msg, message_size, 0);
      }
      //printf("Done sending %d warm up messages.\n", warm_up_cycles);

      measure(message_size, sock, msg);
    }

  close(sock);
  return 0;
}

