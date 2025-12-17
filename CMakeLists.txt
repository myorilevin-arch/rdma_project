cmake_minimum_required(VERSION 3.16)
project(ex3 C)

set(CMAKE_C_STANDARD 11)

add_executable(ex3
        main.c
        RDMA_LIB.c
        RDMA_LIB.h)

