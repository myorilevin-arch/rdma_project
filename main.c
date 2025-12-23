#include "rdma_lib.h"
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

int main(int argc, char *argv[])
{
  if (argc < 2)
  {
    fprintf(stderr, "Usage: %s <server_names>\n", argv[0]);
    return EXIT_FAILURE;
  }

  RDMAContext *pg;
  connect_process_group(argv[1], (void *) &pg);

  char msg_from_left[100];
  char msg_to_right[100] = "Hello from left neighbor!\n";

  struct ibv_mr *mr_recv = ibv_reg_mr(pg->pd, msg_from_left, sizeof(msg_from_left), IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_WRITE);
  if (!mr_recv)
  {
    fprintf(stderr, "Failed to register memory region\n");
    return EXIT_FAILURE;
  }

  struct ibv_mr *mr_send = ibv_reg_mr(pg->pd, msg_to_right, sizeof(msg_to_right), 0);
  if (!mr_send)
  {
    fprintf(stderr, "Failed to register memory region for send\n");
    return EXIT_FAILURE;
  }

  struct ibv_sge recv_sge = {.addr = (uintptr_t) msg_from_left, .length = sizeof(msg_from_left), .lkey = mr_recv->lkey};
  struct ibv_recv_wr recv_wr = {.wr_id = 1, .next = NULL, .sg_list = &recv_sge, .num_sge = 1};
  ibv_post_recv(pg->qp_from_left, &recv_wr, NULL);

  sleep(1);

  struct ibv_sge send_sge = {.addr = (uintptr_t) msg_to_right, .length = sizeof(msg_to_right), .lkey = mr_send->lkey};
  struct ibv_send_wr send_wr = {.wr_id = 2, .next = NULL, .sg_list = &send_sge, .num_sge = 1, .opcode = IBV_WR_SEND, .send_flags = IBV_SEND_SIGNALED};
  ibv_post_send(pg->qp_to_right, &send_wr, NULL);

  struct ibv_wc wc[2];
  int ne = ibv_poll_cq(pg->cq, 2, wc);
  while (ne == 0)
  {
    ne = ibv_poll_cq(pg->cq, 2, wc);
    if (ne < 0)
    {
      fprintf(stderr, "Failed to poll CQ\n");
      return EXIT_FAILURE;
    }
  }
  for (int i = 0; i < ne; i++)
  {
    if (wc[i].status != IBV_WC_SUCCESS)
    {
      fprintf(stderr, "Work completion error: %s\n", ibv_wc_status_str(wc[i].status));
    }
    if (wc[i].opcode == IBV_WC_RECV)
    {
      printf("Received message: %s", msg_from_left);
    }
    else if (wc[i].opcode == IBV_WC_SEND)
    {
      printf("Send completed successfully.\n");
    }
  }

  ibv_dereg_mr(mr_recv);
  ibv_dereg_mr(mr_send);

  return 0;
}
