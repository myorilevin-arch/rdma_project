#include "rdma_lib.h"
#include <netdb.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>

void get_neighbors(const char *servername, char (*my_hostname)[256], char *(*hosts_array)[100], int *right_neighbor_index)
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
  qp_attr.send_cq = context->cq;
  qp_attr.recv_cq = context->cq;
  qp_attr.qp_type = IBV_QPT_RC;
  qp_attr.cap.max_send_wr = 4;
  qp_attr.cap.max_recv_wr = 4;
  qp_attr.cap.max_send_sge = 1;
  qp_attr.cap.max_recv_sge = 1;

  context->qp_to_right = ibv_create_qp(context->pd, &qp_attr);
  if (!context->qp_to_right)
  {
    perror("Failed to create Right QP");
    return EXIT_FAILURE;
  }
  context->qp_from_left = ibv_create_qp(context->pd, &qp_attr);
  if (!context->qp_from_left)
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
  // from reset to init
  struct ibv_qp_attr attr = {0};
  attr.qp_state = IBV_QPS_INIT;
  attr.port_num = 1;
  attr.pkey_index = 0;
  attr.qp_access_flags = IBV_ACCESS_REMOTE_READ | IBV_ACCESS_REMOTE_WRITE | IBV_ACCESS_LOCAL_WRITE;

  int flags = IBV_QP_STATE | IBV_QP_PORT | IBV_QP_PKEY_INDEX | IBV_QP_ACCESS_FLAGS;

  if (ibv_modify_qp(qp, &attr, flags))
  {
    perror("Failed to modify QP to INIT");
    return EXIT_FAILURE;
  }

  // from init to rtr
  memset(&attr, 0, sizeof(attr));
  attr.qp_state = IBV_QPS_RTR;
  attr.dest_qp_num = remote_qpn;
  attr.ah_attr = (struct ibv_ah_attr){
    .is_global = 0,
    .dlid = remote_lid,
    .port_num = 1
  };
  attr.path_mtu = IBV_MTU_1024;
  attr.rq_psn = 0;
  attr.max_dest_rd_atomic = 1;
  attr.min_rnr_timer = 12;

  flags = IBV_QP_STATE | IBV_QP_DEST_QPN | IBV_QP_AV | IBV_QP_PATH_MTU |
          IBV_QP_RQ_PSN | IBV_QP_MAX_DEST_RD_ATOMIC | IBV_QP_MIN_RNR_TIMER;

  if (ibv_modify_qp(qp, &attr, flags))
  {
    perror("Failed to modify QP to RTR");
    return EXIT_FAILURE;
  }

  // from rtr to rts
  memset(&attr, 0, sizeof(attr));
  attr.qp_state = IBV_QPS_RTS;
  attr.sq_psn = 0;
  attr.timeout = 14;
  attr.retry_cnt = 7;
  attr.rnr_retry = 7;
  attr.max_rd_atomic = 1;

  flags = IBV_QP_STATE | IBV_QP_SQ_PSN | IBV_QP_TIMEOUT |
          IBV_QP_RETRY_CNT | IBV_QP_RNR_RETRY | IBV_QP_MAX_QP_RD_ATOMIC;

  if (ibv_modify_qp(qp, &attr, flags))
  {
    perror("Failed to modify QP to RTS");
    return EXIT_FAILURE;
  }
  return EXIT_SUCCESS;
}


int setup_tcp_connection()
{
  return EXIT_SUCCESS;
}
int connect_process_group(char *servername, void **pg_handle)
{
  pg_handle = (RDMAContext) pg_handle;

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
    return EXIT_FAILURE;
  }

  listen(listen_fd, 1);

  struct addrinfo *right_neighbor_info;
  if (getaddrinfo(hosts_array[right_neighbor_index], NULL, NULL, &right_neighbor_info) != 0)
  {
    fprintf(stderr, "Failed to get address info for right neighbor %s\n", hosts_array[right_neighbor_index]);
    return EXIT_FAILURE;
  }

  int right_fd = socket(AF_INET, SOCK_STREAM, 0);
  struct sockaddr_in *right_addr = (struct sockaddr_in *) right_neighbor_info->ai_addr;
  right_addr->sin_family = AF_INET;
  right_addr->sin_port = htons(PORT_BASE);

  if (build_rdma_context(pg_handle) != 0)
  {
    fprintf(stderr, "Failed to build RDMA context\n");
    return EXIT_FAILURE;
  }

  RDMA_exchange_info info_to_neighbors = {pg_handle.lid, pg_handle.qp_from_left->qp_num};
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

  close(left_fd);
  close(right_fd);
  close(listen_fd);

  if (modify_qp_to_rts(pg_handle.qp_from_left, info_from_left_neighbor.qp_num, info_from_left_neighbor.lid) != 0)
  {
    fprintf(stderr, "Failed to modify Left QP to RTS\n");
    return EXIT_FAILURE;
  }
  if (modify_qp_to_rts(pg_handle.qp_to_right, info_from_right_neighbor.qp_num, info_from_right_neighbor.lid) != 0)
  {
    fprintf(stderr, "Failed to modify Right QP to RTS\n");
    return EXIT_FAILURE;
  }

  *pg_handle = pg_handle.ctx;

  return 0;
}
