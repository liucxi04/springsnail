cmake_minimum_required(VERSION 3.10.1)
project(server)

set(CMAKE_CXX_STANDARD 14)

add_executable(server main.cpp conn.h conn.cpp fdwrapper.h log.h mgr.h processpool.h)
