#include "rdma_lib.h"
#include <netdb.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>

typedef struct neighbor_info
{
  char *server_name;
  char *my_hostname;
  char **hosts_array;
  int right_neighbor_index;
  int my_index;
  int servers_count;
} neighbors_info;

void get_neighbors(neighbors_info *info)
{
  gethostname(info->my_hostname, HOSTNAME_MAX);
  char *servername_copy = strdup(info->server_name);
  char *host = strtok(servername_copy, " ");
  while (host != NULL)
  {
    info->hosts_array[info->servers_count++] = host;
    host = strtok(NULL, " ");
  }
  for (int i = 0; i < info->servers_count; i++)
  {
    if (strcmp(info->hosts_array[i], info->my_hostname) == 0)
    {
      info->my_index = i;
      break;
    }
  }
  info->right_neighbor_index = (info->my_index + 1) % info->servers_count;
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

int setup_tcp_connection(char *servername, TCP_context *tcp_ctx, neighbors_info *info)
{
  char my_hostname[256];
  char *hosts_array[100];

  *info = (neighbors_info){servername, my_hostname, hosts_array, -1, -1, 0};
  get_neighbors(info);

  tcp_ctx->listen_fd = socket(AF_INET, SOCK_STREAM, 0);
  int opt = 1;
  setsockopt(tcp_ctx->listen_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

  struct sockaddr_in left_addr;
  left_addr.sin_family = AF_INET;
  left_addr.sin_port = htons(PORT_BASE);
  left_addr.sin_addr.s_addr = INADDR_ANY;

  if (bind(tcp_ctx->listen_fd, (struct sockaddr *) &left_addr, sizeof(left_addr)) < 0)
  {
    perror("bind");
    close(tcp_ctx->listen_fd);
    return EXIT_FAILURE;
  }

  listen(tcp_ctx->listen_fd, 1);

  struct addrinfo *right_neighbor_info;
  if (getaddrinfo(hosts_array[info->right_neighbor_index], NULL, NULL, &right_neighbor_info) != 0)
  {
    fprintf(stderr, "Failed to get address info for right neighbor %s\n", hosts_array[info->right_neighbor_index]);
    return EXIT_FAILURE;
  }

  tcp_ctx->right_fd = socket(AF_INET, SOCK_STREAM, 0);
  struct sockaddr_in *right_addr = (struct sockaddr_in *) right_neighbor_info->ai_addr;
  right_addr->sin_family = AF_INET;
  right_addr->sin_port = htons(PORT_BASE);

  while (connect(tcp_ctx->right_fd, (struct sockaddr *) right_addr, right_neighbor_info->ai_addrlen) == -1)
  {
    sleep(1);
  }
  socklen_t addr_len = sizeof(left_addr);
  tcp_ctx->left_fd = accept(tcp_ctx->listen_fd, (struct sockaddr *) &left_addr, &addr_len);
  return EXIT_SUCCESS;
}

int exchange_rdma_info(const TCP_context *tcp_ctx, const RDMAContext *my_ctx, neighbors_rdma_info *neighbors_info)
{
  RDMA_exchange_info info_to_left = {my_ctx->lid, my_ctx->qp_from_left->qp_num};
  RDMA_exchange_info info_to_right = {my_ctx->lid, my_ctx->qp_to_right->qp_num};
  write(tcp_ctx->left_fd, &info_to_left, sizeof(info_to_left));
  write(tcp_ctx->right_fd, &info_to_right, sizeof(info_to_right));
  read(tcp_ctx->right_fd, &neighbors_info->info_from_right, sizeof(neighbors_info->info_from_right));
  read(tcp_ctx->left_fd, &neighbors_info->info_from_left, sizeof(neighbors_info->info_from_left));
  return 0;
}

int connect_qps_to_rts(const RDMAContext *ctx, const neighbors_rdma_info *neighbors_info)
{
  if (modify_qp_to_rts(ctx->qp_from_left, neighbors_info->info_from_left.qp_num, neighbors_info->info_from_left.lid) != 0)
  {
    fprintf(stderr, "Failed to modify Left QP to RTS\n");
    return EXIT_FAILURE;
  }

  if (modify_qp_to_rts(ctx->qp_to_right, neighbors_info->info_from_right.qp_num, neighbors_info->info_from_right.lid) != 0)
  {
    fprintf(stderr, "Failed to modify Right QP to RTS\n");
    return EXIT_FAILURE;
  }
  return EXIT_SUCCESS;
}

int connect_process_group(char *servername, void **pg_handle)
{
  RDMAContext *my_ctx = malloc(sizeof(RDMAContext));
  if (my_ctx == NULL)
  {
    fprintf(stderr, "Failed to allocate RDMAContext\n");
    return EXIT_FAILURE;
  }
  memset(my_ctx, 0, sizeof(RDMAContext));
  TCP_context tcp_ctx;
  neighbors_info info;
  setup_tcp_connection(servername, &tcp_ctx, &info);
  my_ctx->my_index = info.my_index;
  my_ctx->servers_num = info.servers_count;
  if (build_rdma_context(my_ctx) != 0)
  {
    fprintf(stderr, "Failed to build RDMA context\n");
    return EXIT_FAILURE;
  }

  neighbors_rdma_info neighbors_info = {(RDMA_exchange_info){0}, (RDMA_exchange_info){0}};

  exchange_rdma_info(&tcp_ctx, my_ctx, &neighbors_info);
  // freeaddrinfo(right_neighbor_info);

  // todo - delete later!
  printf("Exchange Done!\n");
  char my_hostname[256];
  gethostname(my_hostname, HOSTNAME_MAX);
  printf("  -> My hostname: %s, I sent to LEFT: LID %d, QPN %d\n", my_hostname, my_ctx->lid, my_ctx->qp_from_left->qp_num);
  printf("  -> My hostname: %s, I sent to RIGHT: LID %d, QPN %d\n", my_hostname, my_ctx->lid, my_ctx->qp_from_left->qp_num);
  printf("  <- My hostname: %s, I got from LEFT: LID %d, QPN %d\n", my_hostname, neighbors_info.info_from_left.lid,
         neighbors_info.info_from_left.qp_num);
  printf("  <- My hostname: %s, I got from RIGHT: LID %d, QPN %d\n", my_hostname, neighbors_info.info_from_right.lid,
         neighbors_info.info_from_right.qp_num);

  close(tcp_ctx.left_fd);
  close(tcp_ctx.right_fd);
  close(tcp_ctx.listen_fd);

  if (connect_qps_to_rts(my_ctx, &neighbors_info) != 0)
  {
    return EXIT_FAILURE;
  }

  *pg_handle = my_ctx;

  return 0;
}

// TODO - check if conversion can be done once at the beginning of the function rather than before each operation
void perform_operation(DATATYPE datatype, OPERATION op, void *recv_buf, void *incoming_buf, int count)
{
  switch (datatype)
  {
    case INT:
      switch (op)
      {
        case SUM:
          for (int i = 0; i < count; i++)
          {
            ((int *) recv_buf)[i] += ((int *) incoming_buf)[i];
          }
          break;
        case MAX:
          for (int i = 0; i < count; i++)
          {
            if (((int *) incoming_buf)[i] > ((int *) recv_buf)[i])
            {
              ((int *) recv_buf)[i] = ((int *) incoming_buf)[i];
            }
          }
          break;
        case MULT:
          for (int i = 0; i < count; i++)
          {
            ((int *) recv_buf)[i] *= ((int *) incoming_buf)[i];
          }
          break;
      }
      break;
    case DOUBLE:
      switch (op)
      {
        case SUM:
          for (int i = 0; i < count; i++)
          {
            ((double *) recv_buf)[i] += ((double *) incoming_buf)[i];
          }
          break;
        case MAX:
          for (int i = 0; i < count; i++)
          {
            if (((double *) incoming_buf)[i] > ((double *) recv_buf)[i])
              ((double *) recv_buf)[i] = ((double *) incoming_buf)[i];
          }
          break;
        case MULT:
          for (int i = 0; i < count; i++)
          {
            ((double *) recv_buf)[i] *= ((double *) incoming_buf)[i];
          }
          break;
      }
      break;
  }
}

int sizeof_datatype(DATATYPE datatype)
{
  switch (datatype)
  {
    case INT:
      return sizeof(int);
    case DOUBLE:
      return sizeof(double);
    default:
      return 0;
  }
}

int pg_reduce_scatter(void *sendbuf, void *recvbuf, int obj_count, DATATYPE datatype, OPERATION op, void *pg_handle)
{
  if (sendbuf != recvbuf)
  {
    memccpy(recvbuf, sendbuf, '\0', obj_count * sizeof_datatype(datatype));
  }
  RDMAContext *pg = (RDMAContext *) pg_handle;

  // TODO - optimize memory registration - register once at the beginning of the program
  struct ibv_mr *mr_recv = ibv_reg_mr(pg->pd, recvbuf, obj_count * sizeof_datatype(datatype), IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_WRITE);
  if (!mr_recv)
  {
    fprintf(stderr, "Failed to register memory region for recv\n");
    return EXIT_FAILURE;
  }
  size_t elements_per_chunk = obj_count / pg->servers_num;
  size_t type_size = sizeof_datatype(datatype);
  size_t chunk_size_in_bytes = elements_per_chunk * type_size;
  void *temp_incoming_buf = malloc(chunk_size_in_bytes);
  if (!temp_incoming_buf)
  {
    fprintf(stderr, "Failed to allocate temp buffer\n");
    return EXIT_FAILURE;
  }

  // TODO - optimize memory registration - register once at the beginning of the program if possible
  struct ibv_mr *mr_temp = ibv_reg_mr(pg->pd, temp_incoming_buf, chunk_size_in_bytes,
                                      IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_WRITE);
  if (!mr_temp)
  {
    fprintf(stderr, "Failed to register memory region for temp buffer\n");
    free(temp_incoming_buf);
    return EXIT_FAILURE;
  }
  for (int i = 0; i < pg->servers_num - 1; i++)
  {
    int send_index = (pg->my_index - i + pg->servers_num) % pg->servers_num;
    int recv_index = (pg->my_index - i - 1 + pg->servers_num) % pg->servers_num;

    size_t send_offset = send_index * chunk_size_in_bytes;
    size_t recv_offset = recv_index * chunk_size_in_bytes;

    // Post receive
    struct ibv_sge recv_sge = {.addr = (uintptr_t) (temp_incoming_buf), .length = chunk_size_in_bytes, .lkey = mr_temp->lkey};
    struct ibv_recv_wr recv_wr = {.wr_id = 1, .next = NULL, .sg_list = &recv_sge, .num_sge = 1};
    ibv_post_recv(pg->qp_from_left, &recv_wr, NULL);

    // Post send
    void *send_address = (char *) recvbuf + send_offset;
    struct ibv_sge send_sge = {
      .addr = (uintptr_t) (send_address), .length = chunk_size_in_bytes,
      .lkey = mr_recv->lkey
    };
    struct ibv_send_wr send_wr = {
      .wr_id = 2, .next = NULL, .sg_list = &send_sge, .num_sge = 1, .opcode = IBV_WR_SEND, .send_flags = IBV_SEND_SIGNALED
    };
    ibv_post_send(pg->qp_to_right, &send_wr, NULL);

    struct ibv_wc wc[2];
    int completions = 0;
    while (completions < 2)
    {
      int ne = ibv_poll_cq(pg->cq, 2 - completions, &wc[completions]);
      if (ne < 0)
      {
        fprintf(stderr, "Failed to poll CQ\n");
        free(temp_incoming_buf);
        return EXIT_FAILURE;
      }
      completions += ne;
    }

    for (int j = 0; j < completions; j++)
    {
      if (wc[j].status != IBV_WC_SUCCESS)
      {
        fprintf(stderr, "Work completion error: %s\n", ibv_wc_status_str(wc[j].status));
        free(temp_incoming_buf);
        return EXIT_FAILURE;
      }
    }
    perform_operation(datatype, op, (char *) recvbuf + recv_offset, temp_incoming_buf, elements_per_chunk);
  }

  free(temp_incoming_buf);
  ibv_dereg_mr(mr_recv);
  ibv_dereg_mr(mr_temp);

  return EXIT_SUCCESS;
}

int pg_all_gather(void *sendbuf, void *recvbuf, int obj_count,
                  DATATYPE datatype, OPERATION op, void *pg_handle)
{
  RDMAContext *pg = (RDMAContext *) pg_handle;

  struct ibv_mr *mr_recv = ibv_reg_mr(pg->pd, recvbuf, obj_count * sizeof_datatype(datatype), IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_WRITE);

  size_t elements_per_chunk = obj_count / pg->servers_num;
  size_t type_size = sizeof_datatype(datatype);
  size_t chunk_size_in_bytes = elements_per_chunk * type_size;

  for (int i = 0; i < pg->servers_num; i++)
  {
    int send_index = (pg->my_index - i + 1) % pg->servers_num;
    int recv_index = (pg->my_index - i) % pg->servers_num;

    size_t send_offset = send_index * chunk_size_in_bytes;
    size_t recv_offset = recv_index * chunk_size_in_bytes;

    // Post Receive
    struct ibv_sge recv_sge = {.addr = (uintptr_t) ((char *) recvbuf + recv_offset), .length = chunk_size_in_bytes, .lkey = mr_recv->lkey};
    struct ibv_recv_wr recv_wr = {.wr_id = 1, .next = NULL, .sg_list = &recv_sge, .num_sge = 1};
    ibv_post_recv(pg->qp_from_left, &recv_wr, NULL);

    // Post Send
    struct ibv_sge send_sge = {.addr = (uintptr_t) ((char *) recvbuf + send_offset), .length = chunk_size_in_bytes, .lkey = mr_recv->lkey};
    struct ibv_send_wr send_wr = {
      .wr_id = 2, .next = NULL, .sg_list = &send_sge, .num_sge = 1, .opcode = IBV_WR_SEND, .send_flags = IBV_SEND_SIGNALED
    };
    ibv_post_send(pg->qp_to_right, &send_wr, NULL);
  }
  struct ibv_wc wc[2];
  int completions = 0;
  while (completions < 2)
  {
    int ne = ibv_poll_cq(pg->cq, 2 - completions, &wc[completions]);
    if (ne < 0)
    {
      fprintf(stderr, "Failed to poll CQ\n");
      return EXIT_FAILURE;
    }
    completions += ne;
  }

  for (int j = 0; j < completions; j++)
  {
    if (wc[j].status != IBV_WC_SUCCESS)
    {
      fprintf(stderr, "Work completion error: %s\n", ibv_wc_status_str(wc[j].status));
      return EXIT_FAILURE;
    }
  }
  ibv_dereg_mr(mr_recv);
  return EXIT_SUCCESS;
}

int pg_all_reduce(void *sendbuf, void *recvbuf, int count, DATATYPE datatype, OPERATION op, void *pg_handle)
{
  if (pg_reduce_scatter(sendbuf, recvbuf, count, datatype, op, pg_handle)) != 0)
  {
    return EXIT_FAILURE;
  }
  if (pg_all_gather(recvbuf, recvbuf, count, datatype, op, pg_handle) != 0)
  {
    return EXIT_FAILURE;
  }
  return EXIT_SUCCESS;
}
