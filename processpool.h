#ifndef PROCESSPOOL_H
#define PROCESSPOOL_H

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <cassert>
#include <cstdio>
#include <unistd.h>
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <cstdlib>
#include <sys/epoll.h>
#include <csignal>
#include <sys/wait.h>
#include <sys/stat.h>
#include <vector>
#include "log.h"
#include "fdwrapper.h"

using std::vector;

/**
 * 子进程类
 * */
class process {
public:
    process() : m_pid(-1) {}

public:
    int m_busy_ratio{}; // 每台实际处理服务器（业务逻辑服务器）的权重，数值表示正在处理的客户端数量
    pid_t m_pid; // 目标子进程的PID
    int m_pipe_fd[2]{}; // 父进程和子进程通信用的管道,父进程给子进程通知事件，子进程给父进程发送加权比
};

template<typename C, typename H, typename M>
class processpool {
private:
    explicit processpool(int listen_fd, int process_number = 8);

public:
    static processpool<C, H, M> *create(int listen_fd, int process_number = 8) {
        if (!m_instance) {
            m_instance = new processpool<C, H, M>(listen_fd, process_number);
        }
        return m_instance;
    }

    ~processpool() {
        delete[] m_sub_process;
    }

    void run(const vector<H> &arg);

private:
    void notify_parent_busy_ratio(int pipe_fd, M *manager); //获取目前连接数量，将其发送给父进程

    int get_most_free_srv(); // 找出最空闲的服务器

    void setup_sig_pipe(); // 统一事件源

    void run_parent();

    void run_child(const vector<H> &arg);

private:
    static const int MAX_PROCESS_NUMBER = 16;
    static const int USER_PER_PROCESS = 65535;
    static const int MAX_EVENT_NUMBER = 10000;
    int m_process_number;
    int m_idx; //子进程在池中的序号（从0开始）
    int m_epoll_fd;
    int m_listen_fd;
    int m_stop;
    process *m_sub_process;
    static processpool<C, H, M> *m_instance;
};

template<typename C, typename H, typename M>
processpool<C, H, M> *processpool<C, H, M>::m_instance = NULL;

static int EPOLL_WAIT_TIME = 5000;
static int sig_pipe_fd[2];

static void sig_handler(int sig) {
    int save_errno = errno;
    int msg = sig;
    send(sig_pipe_fd[1], (char *) &msg, 1, 0);
    errno = save_errno;
}

static void addsig(int sig, void( handler )(int), bool restart = true) {
    struct sigaction sa{};
    memset(&sa, '\0', sizeof(sa));
    sa.sa_handler = handler;
    if (restart) {
        sa.sa_flags |= SA_RESTART;
    }
    sigfillset(&sa.sa_mask);
    assert(sigaction(sig, &sa, nullptr) != -1);
}

template<typename C, typename H, typename M>
processpool<C, H, M>::processpool(int listen_fd, int process_number)
        : m_listen_fd(listen_fd), m_process_number(process_number), m_idx(-1), m_stop(false), m_epoll_fd(-1){
    assert((process_number > 0) && (process_number <= MAX_PROCESS_NUMBER));

    m_sub_process = new process[process_number];
    assert(m_sub_process);

    for (int i = 0; i < process_number; ++i) {
        int ret = socketpair(PF_UNIX, SOCK_STREAM, 0, m_sub_process[i].m_pipe_fd);
        assert(ret == 0);

        m_sub_process[i].m_pid = fork();
        assert(m_sub_process[i].m_pid >= 0);
        if (m_sub_process[i].m_pid > 0) {
            close(m_sub_process[i].m_pipe_fd[1]);
            m_sub_process[i].m_busy_ratio = 0;
            continue;
        } else {
            close(m_sub_process[i].m_pipe_fd[0]);
            m_idx = i;
            break;
        }
    }
}

template<typename C, typename H, typename M>
int processpool<C, H, M>::get_most_free_srv() {
    int ratio = m_sub_process[0].m_busy_ratio;
    int idx = 0;
    for (int i = 0; i < m_process_number; ++i) {
        if (m_sub_process[i].m_busy_ratio < ratio) {
            idx = i;
            ratio = m_sub_process[i].m_busy_ratio;
        }
    }
    return idx;
}

template<typename C, typename H, typename M>
void processpool<C, H, M>::setup_sig_pipe() {
    m_epoll_fd = epoll_create(5);
    assert(m_epoll_fd != -1);

    int ret = socketpair(PF_UNIX, SOCK_STREAM, 0, sig_pipe_fd);
    assert(ret != -1);

    set_non_blocking(sig_pipe_fd[1]);
    add_read_fd(m_epoll_fd, sig_pipe_fd[0]);

    addsig(SIGCHLD, sig_handler);
    addsig(SIGTERM, sig_handler);
    addsig(SIGINT, sig_handler);
    addsig(SIGPIPE, SIG_IGN);
}

template<typename C, typename H, typename M>
void processpool<C, H, M>::run(const vector<H> &arg) {
    if (m_idx != -1) {
        run_child(arg);
        return;
    }
    run_parent();
}

template<typename C, typename H, typename M>
void processpool<C, H, M>::notify_parent_busy_ratio(int pipe_fd, M *manager) {
    int msg = manager->get_used_conn_cnt();
    send(pipe_fd, (char *) &msg, 1, 0);
}

template<typename C, typename H, typename M>
void processpool<C, H, M>::run_child(const vector<H> &arg) {
    setup_sig_pipe();

    int pipe_fd_read = m_sub_process[m_idx].m_pipe_fd[1];
    add_read_fd(m_epoll_fd, pipe_fd_read);

    epoll_event events[MAX_EVENT_NUMBER];

    M *manager = new M(m_epoll_fd, arg[m_idx]);
    assert(manager);

    int number;
    ssize_t ret;

    while (!m_stop) {
        number = epoll_wait(m_epoll_fd, events, MAX_EVENT_NUMBER, EPOLL_WAIT_TIME);
        if ((number < 0) && (errno != EINTR)) {
            log(LOG_ERR, __FILE__, __LINE__, "%s", "epoll failure");
            break;
        }

        if (number == 0) {
            manager->recycle_conns();
            continue;
        }

        for (int i = 0; i < number; i++) {
            int sock_fd = events[i].data.fd;
            // 来自父进程的消息
            if ((sock_fd == pipe_fd_read) && (events[i].events & EPOLLIN)) {
                int client = 0;
                ret = recv(sock_fd, (char *) &client, sizeof(client), 0);
                if (((ret < 0) && (errno != EAGAIN)) || ret == 0) {
                    continue;
                } else {
                    struct sockaddr_in client_address{};
                    socklen_t client_length = sizeof(client_address);
                    int conn_fd = accept(m_listen_fd, (struct sockaddr *) &client_address, &client_length);
                    if (conn_fd < 0) {
                        log(LOG_ERR, __FILE__, __LINE__, "errno: %s", strerror(errno));
                        continue;
                    }
                    add_read_fd(m_epoll_fd, conn_fd);
                    C *conn = manager->pick_conn(conn_fd);
                    if (!conn) {
                        close_fd(m_epoll_fd, conn_fd);
                        continue;
                    }
                    conn->init_clt(conn_fd, client_address);
                    notify_parent_busy_ratio(pipe_fd_read, manager);
                }
            // 有需要处理的信号
            } else if ((sock_fd == sig_pipe_fd[0]) && (events[i].events & EPOLLIN)) {
                char signals[1024];
                ret = recv(sig_pipe_fd[0], signals, sizeof(signals), 0);
                if (ret <= 0) {
                    continue;
                } else {
                    for (int j = 0; j < ret; ++j) {
                        switch (signals[j]) {
                            case SIGCHLD: {
                                int stat;
                                while (waitpid(-1, &stat, WNOHANG) > 0) {}
                                break;
                            }
                            case SIGTERM:
                            case SIGINT: {
                                m_stop = true;
                                break;
                            }
                            default: {
                                break;
                            }
                        }
                    }
                }
            // 其他输入事件
            } else if (events[i].events & EPOLLIN) {
                RET_CODE result = manager->process(sock_fd, READ);
                switch (result) {
                    case CLOSED: {
                        notify_parent_busy_ratio(pipe_fd_read, manager);
                        break;
                    }
                    default:
                        break;
                }
            // 输出事件
            } else if (events[i].events & EPOLLOUT) {
                RET_CODE result = manager->process(sock_fd, WRITE);
                switch (result) {
                    case CLOSED: {
                        notify_parent_busy_ratio(pipe_fd_read, manager);
                        break;
                    }
                    default:
                        break;
                }
            } else {
                continue;
            }
        }
    }

    close(pipe_fd_read);
    close(m_epoll_fd);
}

template<typename C, typename H, typename M>
void processpool<C, H, M>::run_parent() {
    setup_sig_pipe();

    for (int i = 0; i < m_process_number; ++i) {
        add_read_fd(m_epoll_fd, m_sub_process[i].m_pipe_fd[0]);
    }

    add_read_fd(m_epoll_fd, m_listen_fd);

    epoll_event events[MAX_EVENT_NUMBER];
    int new_conn = 1;
    int number;
    ssize_t ret;

    while (!m_stop) {
        number = epoll_wait(m_epoll_fd, events, MAX_EVENT_NUMBER, EPOLL_WAIT_TIME);
        if ((number < 0) && (errno != EINTR)) {
            log(LOG_ERR, __FILE__, __LINE__, "%s", "epoll failure");
            break;
        }

        for (int i = 0; i < number; i++) {
            int sock_fd = events[i].data.fd;
            if (sock_fd == m_listen_fd) {
                int idx = get_most_free_srv();
                send(m_sub_process[idx].m_pipe_fd[0], (char *) &new_conn, sizeof(new_conn), 0);
                log(LOG_INFO, __FILE__, __LINE__, "send request to child %d", idx);
            // 有信号需要处理
            } else if ((sock_fd == sig_pipe_fd[0]) && (events[i].events & EPOLLIN)) {
                char signals[1024];
                ret = recv(sig_pipe_fd[0], signals, sizeof(signals), 0);
                if (ret <= 0) {
                    continue;
                } else {
                    for (int j = 0; j < ret; ++j) {
                        switch (signals[j]) {
                            case SIGCHLD: {
                                pid_t pid;
                                int stat;
                                while ((pid = waitpid(-1, &stat, WNOHANG)) > 0) {
                                    for (int k = 0; k < m_process_number; ++k) {
                                        if (m_sub_process[k].m_pid == pid) {
                                            log(LOG_INFO, __FILE__, __LINE__, "child %d join", k);
                                            close(m_sub_process[k].m_pipe_fd[0]);
                                            m_sub_process[k].m_pid = -1;
                                        }
                                    }
                                }
                                m_stop = true;
                                for (int k = 0; k < m_process_number; ++k) {
                                    if (m_sub_process[k].m_pid != -1) {
                                        m_stop = false;
                                    }
                                }
                                break;
                            }
                            case SIGTERM:
                            case SIGINT: {
                                log(LOG_INFO, __FILE__, __LINE__, "%s", "kill all the child now");
                                for (int k = 0; k < m_process_number; ++k) {
                                    int pid = m_sub_process[k].m_pid;
                                    if (pid != -1) {
                                        kill(pid, SIGTERM);
                                    }
                                }
                                break;
                            }
                            default: {
                                break;
                            }
                        }
                    }
                }
            } else if (events[i].events & EPOLLIN) {
                int busy_ratio = 0;
                ret = recv(sock_fd, (char *) &busy_ratio, sizeof(busy_ratio), 0);
                if (((ret < 0) && (errno != EAGAIN)) || ret == 0) {
                    continue;
                }
                // 更新负载均衡参数
                for (int j = 0; j < m_process_number; ++j) {
                    if (sock_fd == m_sub_process[j].m_pipe_fd[0]) {
                        m_sub_process[j].m_busy_ratio = busy_ratio;
                        break;
                    }
                }
                continue;
            }
        }
    }

    for (int i = 0; i < m_process_number; ++i) {
        close_fd(m_epoll_fd, m_sub_process[i].m_pipe_fd[0]);
    }
    close(m_epoll_fd);
}

#endif
