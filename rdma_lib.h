#ifndef EX3_RDMA_LIB_H
#define EX3_RDMA_LIB_H

#include <stdint.h>
#include <infiniband/verbs.h>

#define HOSTNAME_MAX 256
#define PORT_BASE 33488

typedef struct RDMAContext
{
  struct ibv_context *ctx;
  struct ibv_pd *pd;
  struct ibv_cq *cq;
  struct ibv_qp * qp_to_right;
  struct ibv_qp * qp_from_left;

  uint16_t lid;
  uint8_t port_num;
} RDMAContext;


typedef struct RDMA_exchange_info {
  uint16_t lid;
  uint32_t qp_num;
} RDMA_exchange_info;


int connect_process_group(char *servername, void **pg_handle);

#endif //EX3_RDMA_LIB_H
