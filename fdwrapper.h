#ifndef FDWRAPPER_H
#define FDWRAPPER_H

/**
 * 一系列的辅助函数，用来操作 fd
 * */

#include <unistd.h>
#include <fcntl.h>
#include <sys/epoll.h>

enum RET_CODE {
    OK = 0, NOTHING = 1, IO_ERR = -1, CLOSED = -2, BUFFER_FULL = -3, BUFFER_EMPTY = -4, TRY_AGAIN
};
enum OP_TYPE {
    READ = 0, WRITE, ERROR
};

int set_non_blocking(int fd);

void add_read_fd(int epoll_fd, int fd);

void add_write_fd(int epoll_fd, int fd);

void remove_fd(int epoll_fd, int fd);

void close_fd(int epoll_fd, int fd);

void mod_fd(int epoll_fd, int fd, int ev);

#endif