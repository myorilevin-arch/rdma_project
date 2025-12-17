#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>

// --- Configuration ---
#define BASE_PORT 50000

const char* SERVER_HOSTS[] = {
    "mlx-stud-01",
    "mlx-stud-02",
    "mlx-stud-03",
    "mlx-stud-04"
};
#define NUM_SERVERS (sizeof(SERVER_HOSTS) / sizeof(SERVER_HOSTS[0]))

// --- Main Logic ---
int main(int argc, char *argv[]) {
    // --- 1. Argument Parsing ---
    if (argc < 3 || strcmp(argv[1], "-myindex") != 0) {
        fprintf(stderr, "Usage: %s -myindex <rank>\n", argv[0]);
        exit(EXIT_FAILURE);
    }
    int my_rank = atoi(argv[2]);
    if (my_rank < 0 || my_rank >= NUM_SERVERS) {
        fprintf(stderr, "Invalid rank. Must be between 0 and %d.\n", NUM_SERVERS - 1);
        exit(EXIT_FAILURE);
    }

    // --- 2. Neighbor Calculation ---
    int next_rank = (my_rank + 1) % NUM_SERVERS;
    const char* next_hostname = SERVER_HOSTS[next_rank];
    int prev_rank = (my_rank - 1 + NUM_SERVERS) % NUM_SERVERS;

    printf("[Rank %d] My Role: Accept from %d, Connect to %d (%s)\n",
           my_rank, prev_rank, next_rank, next_hostname);

    // --- 3. Setup Listening Socket (Server Role) ---
    // We need TWO different file descriptors: one for listening, one for the final accepted connection.
    int listening_fd, incoming_fd;
    listening_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (listening_fd < 0) {
        perror("listening socket creation failed");
        exit(EXIT_FAILURE);
    }

    struct sockaddr_in my_addr;
    memset(&my_addr, 0, sizeof(my_addr));
    my_addr.sin_family = AF_INET;
    my_addr.sin_addr.s_addr = INADDR_ANY;
    my_addr.sin_port = htons(BASE_PORT + my_rank); 

    if (bind(listening_fd, (struct sockaddr *)&my_addr, sizeof(my_addr)) < 0) {
        perror("bind failed");
        exit(EXIT_FAILURE);
    }

    if (listen(listening_fd, 1) < 0) {
        perror("listen failed");
        exit(EXIT_FAILURE);
    }
    
    // Brief sleep to allow all processes to start listening before others start connecting
    sleep(2);

    // --- 4. Setup Outgoing Connection (Client Role) ---
    int outgoing_fd = socket(AF_INET, SOCK_STREAM, 0);
    struct hostent *next_server_info = gethostbyname(next_hostname);
    if (next_server_info == NULL) {
        fprintf(stderr, "ERROR, no such host as %s\n", next_hostname);
        exit(EXIT_FAILURE);
    }

    struct sockaddr_in next_addr;
    memset(&next_addr, 0, sizeof(next_addr));
    next_addr.sin_family = AF_INET;
    next_addr.sin_port = htons(BASE_PORT + next_rank);
    memcpy(&next_addr.sin_addr.s_addr, next_server_info->h_addr_list[0], next_server_info->h_length);
    
    if (connect(outgoing_fd, (struct sockaddr *)&next_addr, sizeof(next_addr)) < 0) {
        perror("connect to next neighbor failed");
        exit(EXIT_FAILURE);
    }
    printf("[Rank %d] Successfully connected to rank %d.\n", my_rank, next_rank);

    // --- 5. Accept Incoming Connection ---
    struct sockaddr_in prev_addr;
    socklen_t prev_addr_len = sizeof(prev_addr);
    incoming_fd = accept(listening_fd, (struct sockaddr *)&prev_addr, &prev_addr_len);
    if (incoming_fd < 0) {
        perror("accept from previous neighbor failed");
        exit(EXIT_FAILURE);
    }
    printf("[Rank %d] Accepted connection from rank %d.\n", my_rank, prev_rank);

    printf("--- [Rank %d] Ring connection established! ---\n", my_rank);

    // --- 6. Simple Message Passing Test (Token Ring) ---
    if (my_rank == 0) {
        const char* message = "Hello Ring!";
        printf("[Rank 0] Sending initial message: '%s'\n", message);
        write(outgoing_fd, message, strlen(message) + 1); // +1 for null terminator
        
        char buffer[1024] = {0};
        read(incoming_fd, buffer, sizeof(buffer));
        printf("[Rank 0] Received final message: '%s'. Test successful.\n", buffer);
    } else {
        char buffer[1024] = {0};
        read(incoming_fd, buffer, sizeof(buffer));
        printf("[Rank %d] Received message: '%s'\n", my_rank, buffer);
        
        // Forward the exact same message to the next process
        write(outgoing_fd, buffer, strlen(buffer) + 1);
        printf("[Rank %d] Forwarded message to rank %d.\n", my_rank, next_rank);
    }

    // --- 7. Cleanup ---
    close(outgoing_fd);
    close(incoming_fd);
    close(listening_fd);
    
    return 0;
}