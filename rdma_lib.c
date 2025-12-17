#include "rdma_lib.h"
#include <netdb.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>

void get_neighbors(char *servername, char (*my_hostname)[256], char *(*hosts_array)[100], int *right_neighbor_index)
{
  gethostname(*my_hostname, HOSTNAME_MAX);
  char *servername_copy = strdup(servername);
  int servers_count = 0;
  char *host = strtok(servername_copy, " ");
  while (host != NULL)
  {
    (*hosts_array)[servers_count++] = host;
    host = strtok(NULL, " ");
  }
  unsigned int my_hostname_index = -1;
  for (int i = 0; i < servers_count; i++)
  {
    if (strcmp((*hosts_array)[i], *my_hostname) == 0)
    {
      my_hostname_index = i;
      break;
    }
  }
  *right_neighbor_index = (my_hostname_index + 1) % servers_count;
  // int left_neighbor_index = (my_hostname_index - 1 + servers_count) % servers_count;
}

int build_rdma_context(RDMAContext *context)
{
  struct ibv_device **dev_list = ibv_get_device_list(NULL);
  if (!dev_list)
  {
    perror("Failed to get IB devices list");
    return EXIT_FAILURE;
  }
  if (!dev_list[0])
  {
    perror("No IB devices found");
    return EXIT_FAILURE;
  }
  context->ctx = ibv_open_device(dev_list[0]);
  if (!context->ctx)
  {
    perror("Failed to open IB device");
    return EXIT_FAILURE;
  }
  context->pd = ibv_alloc_pd(context->ctx);
  if (!context->pd)
  {
    perror("Failed to allocate Protection Domain");
    return EXIT_FAILURE;
  }
  context->cq = ibv_create_cq(context->ctx, 4, NULL, NULL, 0);
  if (!context->cq)
  {
    perror("Failed to create Completion Queue");
    return EXIT_FAILURE;
  }
  struct ibv_qp_init_attr qp_attr = {0};
  // memset(&qp_attr, 0, sizeof(qp_attr));
  qp_attr.send_cq = context->cq;
  qp_attr.recv_cq = context->cq;
  qp_attr.qp_type = IBV_QPT_RC;
  qp_attr.cap.max_send_wr = 4;
  qp_attr.cap.max_recv_wr = 4;
  qp_attr.cap.max_send_sge = 1;
  qp_attr.cap.max_recv_sge = 1;

  context->right_qp = ibv_create_qp(context->pd, &qp_attr);
  if (!context->right_qp)
  {
    perror("Failed to create Right QP");
    return EXIT_FAILURE;
  }
  context->left_qp = ibv_create_qp(context->pd, &qp_attr);
  if (!context->left_qp)
  {
    perror("Failed to create Left QP");
    return EXIT_FAILURE;
  }

  struct ibv_port_attr port_attr;
  context->port_num = 1;
  if (ibv_query_port(context->ctx, context->port_num, &port_attr))
  {
    perror("Failed to query port");
    return EXIT_FAILURE;
  }
  context->lid = port_attr.lid;
  return EXIT_SUCCESS;
}

int modify_qp_to_rts(struct ibv_qp *qp, uint32_t remote_qpn, uint16_t remote_lid)
{

}

int connect_process_group(char *servername, void **pg_handle)
{
  char my_hostname[256];
  char *hosts_array[100];
  int right_neighbor_index;
  // int left_neighbor_index;

  get_neighbors(servername, &my_hostname, &hosts_array, &right_neighbor_index);

  int listen_fd = socket(AF_INET, SOCK_STREAM, 0);
  int opt = 1;
  setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

  struct sockaddr_in left_addr;
  left_addr.sin_family = AF_INET;
  left_addr.sin_port = htons(PORT_BASE);
  left_addr.sin_addr.s_addr = INADDR_ANY;

  if (bind(listen_fd, (struct sockaddr *) &left_addr, sizeof(left_addr)) < 0)
  {
    perror("bind");
    close(listen_fd);
    exit(EXIT_FAILURE);
  }

  listen(listen_fd, 1);

  struct addrinfo *right_neighbor_info;
  if (getaddrinfo(hosts_array[right_neighbor_index], NULL, NULL, &right_neighbor_info) != 0)
  {
    fprintf(stderr, "Failed to get address info for right neighbor %s\n", hosts_array[right_neighbor_index]);
    exit(EXIT_FAILURE);
  }

  int right_fd = socket(AF_INET, SOCK_STREAM, 0);
  struct sockaddr_in *right_addr = (struct sockaddr_in *) right_neighbor_info->ai_addr;
  right_addr->sin_family = AF_INET;
  right_addr->sin_port = htons(PORT_BASE);

  RDMAContext rdma_context;
  if (build_rdma_context(&rdma_context) != 0)
  {
    fprintf(stderr, "Failed to build RDMA context\n");
    exit(EXIT_FAILURE);
  }

  RDMA_exchange_info info_to_neighbors = {rdma_context.lid, rdma_context.left_qp->qp_num};
  RDMA_exchange_info info_from_right_neighbor;
  RDMA_exchange_info info_from_left_neighbor;

  while (connect(right_fd, (struct sockaddr *) right_addr, right_neighbor_info->ai_addrlen) == -1)
  {
    sleep(1);
  }
  socklen_t addr_len = sizeof(left_addr);
  int left_fd = accept(listen_fd, (struct sockaddr *) &left_addr, &addr_len);

  freeaddrinfo(right_neighbor_info);

  write(left_fd, &info_to_neighbors, sizeof(info_to_neighbors));
  write(right_fd, &info_to_neighbors, sizeof(info_to_neighbors));
  read(right_fd, &info_from_right_neighbor, sizeof(info_from_right_neighbor));
  read(left_fd, &info_from_left_neighbor, sizeof(info_from_left_neighbor));

  printf("Exchange Done!\n");
  printf("  -> My hostname: %s, I sent to LEFT: LID %d, QPN %d\n", my_hostname, info_to_neighbors.lid, info_to_neighbors.qp_num);
  printf("  -> My hostname: %s, I sent to RIGHT: LID %d, QPN %d\n", my_hostname, info_to_neighbors.lid, info_to_neighbors.qp_num);
  printf("  <- My hostname: %s, I got from LEFT: LID %d, QPN %d\n", my_hostname, info_from_left_neighbor.lid, info_from_left_neighbor.qp_num);
  printf("  <- My hostname: %s, I got from RIGHT: LID %d, QPN %d\n", my_hostname, info_from_right_neighbor.lid, info_from_right_neighbor.qp_num);




  return 0;
}
