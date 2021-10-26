cmake_minimum_required(VERSION 3.19)
project(lab1 C)

set(CMAKE_C_STANDARD 99)

set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -fsanitize=address -Wall -pedantic")

add_executable(lab1 pa1/main.c pa1/ipc.c)