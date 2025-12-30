#include "rdma_lib.h"
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#define VECTOR_SIZE 4

int main(int argc, char *argv[])
{
  if (argc < 2)
  {
    fprintf(stderr, "Usage: %s <server_names>\n", argv[0]);
    return EXIT_FAILURE;
  }

  RDMAContext *pg;
  connect_process_group(argv[1], (void *) &pg);

  printf("--- Rank %d / %d is ready ---\n", pg->my_index, pg->servers_num);

  int data_buf[VECTOR_SIZE];
  int fill_value = pg->my_index + 1;
  for (int i = 0; i < VECTOR_SIZE; i++)
  {
    data_buf[i] = fill_value;
  }

  printf("Input: [ %d, %d, %d, %d ]\n", data_buf[0], data_buf[1], data_buf[2], data_buf[3]);

  int ret = pg_all_reduce(data_buf, data_buf, VECTOR_SIZE, INT, SUM, pg);

  if (ret != 0)
  {
    fprintf(stderr, "pg_reduce_scatter failed!\n");
    return EXIT_FAILURE;
  }

  printf("Output: [ %d, %d, %d, %d ]\n", data_buf[0], data_buf[1], data_buf[2], data_buf[3]);

  free(pg);

  return 0;
}
