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
  struct ibv_qp *qp_to_right;
  struct ibv_qp *qp_from_left;
  uint16_t lid;
  uint8_t port_num;
  int my_index;
  int servers_num;
} RDMAContext;

typedef struct TCP_context
{
  int listen_fd;
  int left_fd;
  int right_fd;
} TCP_context;

typedef struct RDMA_exchange_info
{
  uint16_t lid;
  uint32_t qp_num;
} RDMA_exchange_info;

typedef struct neighbors_rdma_info
{
  RDMA_exchange_info info_from_left;
  RDMA_exchange_info info_from_right;
} neighbors_rdma_info;

typedef enum
{
  INT,
  DOUBLE
} DATATYPE;

typedef enum
{
  SUM,
  MAX,
  MULT
} OPERATION;

int connect_process_group(char *servername, void **pg_handle);

int pg_reduce_scatter(void *sendbuf, void *recvbuf, int obj_count,
                      DATATYPE datatype, OPERATION op, void *pg_handle);

int pg_all_gather(void *sendbuf, void *recvbuf, int count,
                  DATATYPE datatype, OPERATION op, void *pg_handle);

int pg_all_reduce(void *sendbuf, void *recvbuf, int count, DATATYPE datatype, OPERATION op, void *pg_handle);

int pg_close(void *pg_handle); /* Destroys the QP */

#endif //EX3_RDMA_LIB_H
