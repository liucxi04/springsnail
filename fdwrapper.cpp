#include <unistd.h>
#include <fcntl.h>
#include <sys/epoll.h>

// 设置 fd 非阻塞
int set_non_blocking(int fd) {
    int old_option = fcntl(fd, F_GETFL);
    int new_option = old_option | O_NONBLOCK;
    fcntl(fd, F_SETFL, new_option);
    return old_option;
}

// 向 epoll_fd 注册读事件
void add_read_fd(int epoll_fd, int fd) {
    epoll_event event{};
    event.data.fd = fd;
    event.events = EPOLLIN | EPOLLET;
    epoll_ctl(epoll_fd, EPOLL_CTL_ADD, fd, &event);
    set_non_blocking(fd);
}

// 向 epoll_fd 注册写事件，没有用到
void add_write_fd(int epoll_fd, int fd) {
    epoll_event event{};
    event.data.fd = fd;
    event.events = EPOLLOUT | EPOLLET;
    epoll_ctl(epoll_fd, EPOLL_CTL_ADD, fd, &event);
    set_non_blocking(fd);
}

// 从 epoll_fd 删除 fd 上的所有事件
void close_fd(int epoll_fd, int fd) {
    epoll_ctl(epoll_fd, EPOLL_CTL_DEL, fd, nullptr);
    close(fd);
}

// 修改 epoll_fd 上 fd 注册的事件
void mod_fd(int epoll_fd, int fd, int ev) {
    epoll_event event{};
    event.data.fd = fd;
    event.events = ev | EPOLLET;
    epoll_ctl(epoll_fd, EPOLL_CTL_MOD, fd, &event);
}
