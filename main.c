#include "rdma_lib.h"
#include <stdio.h>
#include <stdlib.h>

int main(int argc, char *argv[])
{
  if (argc < 2)
  {
    fprintf(stderr, "Usage: %s <server_names>\n", argv[0]);
    return EXIT_FAILURE;
  }

  RDMAContext pg;
  connect_process_group(argv[1], (void **) &pg);
  return 0;
}
