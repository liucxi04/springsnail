#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <string.h>
#include <sys/epoll.h>

#include <exception>
#include "log.h"
#include "mgr.h"

using std::pair;

int mgr::m_epoll_fd = -1;

mgr::mgr(int epoll_fd, const host &srv) : m_logic_srv(srv) {
    m_epoll_fd = epoll_fd;
    struct sockaddr_in address;
    bzero(&address, sizeof(address));
    address.sin_family = AF_INET;
    inet_pton(AF_INET, srv.m_hostname, &address.sin_addr);
    address.sin_port = htons(srv.m_port);
    log(LOG_INFO, __FILE__, __LINE__, "logical srv host info: (%s, %d)", srv.m_hostname, srv.m_port);

    for (int i = 0; i < srv.m_conn_cnt; ++i) {
        sleep(1);
        int sock_fd = conn2srv(address);
        if (sock_fd < 0) {
            log(LOG_ERR, __FILE__, __LINE__, "build connection %d failed", i);
        } else {
            log(LOG_INFO, __FILE__, __LINE__, "build connection %d to server success", i);
            conn *tmp = NULL;
            try {
                tmp = new conn;
            }
            catch (...) {
                close(sock_fd);
                continue;
            }
            tmp->init_srv(sock_fd, address);
            m_conns.insert(pair<int, conn *>(sock_fd, tmp));
        }
    }
}

mgr::~mgr() {
}

int mgr::conn2srv(const sockaddr_in &address) {
    int sock_fd = socket(PF_INET, SOCK_STREAM, 0);
    if (sock_fd < 0) {
        return -1;
    }

    if (connect(sock_fd, (struct sockaddr *) &address, sizeof(address)) != 0) {
        close(sock_fd);
        return -1;
    }
    return sock_fd;
}

int mgr::get_used_conn_cnt() {
    return m_used.size();
}

// 输入参数为客户端连接，负载均衡服务器作为中转，将客户端连接与服务器绑定
conn *mgr::pick_conn(int clt_fd) {
    if (m_conns.empty()) {
        log(LOG_ERR, __FILE__, __LINE__, "%s", "not enough srv connections to server");
        return NULL;
    }

    map<int, conn *>::iterator iter = m_conns.begin();
    int srv_fd = iter->first;
    conn *tmp = iter->second;
    if (!tmp) {
        log(LOG_ERR, __FILE__, __LINE__, "%s", "empty server connection object");
        return NULL;
    }
    m_conns.erase(iter);
    m_used.insert(pair<int, conn *>(clt_fd, tmp));
    m_used.insert(pair<int, conn *>(srv_fd, tmp));
    add_read_fd(m_epoll_fd, clt_fd);
    add_read_fd(m_epoll_fd, srv_fd);
    log(LOG_INFO, __FILE__, __LINE__, "bind client sock %d with server sock %d", clt_fd, srv_fd);
    return tmp;
}

void mgr::free_conn(conn *connection) {
    int clt_fd = connection->m_clt_fd;
    int srv_fd = connection->m_srv_fd;
    close_fd(m_epoll_fd, clt_fd);
    close_fd(m_epoll_fd, srv_fd);
    m_used.erase(clt_fd);
    m_used.erase(srv_fd);
    connection->reset();
    m_freed.insert(pair<int, conn *>(srv_fd, connection));
}

void mgr::recycle_conns() {
    if (m_freed.empty()) {
        return;
    }
    for (map<int, conn *>::iterator iter = m_freed.begin(); iter != m_freed.end(); iter++) {
        sleep(1);
        int srv_fd = iter->first;
        conn *tmp = iter->second;
        srv_fd = conn2srv(tmp->m_srv_address);
        if (srv_fd < 0) {
            log(LOG_ERR, __FILE__, __LINE__, "%s", "fix connection failed");
        } else {
            log(LOG_INFO, __FILE__, __LINE__, "%s", "fix connection success");
            tmp->init_srv(srv_fd, tmp->m_srv_address);
            m_conns.insert(pair<int, conn *>(srv_fd, tmp));
        }
    }
    m_freed.clear();
}

RET_CODE mgr::process(int fd, OP_TYPE type) {
    conn *connection = m_used[fd];
    if (!connection) {
        return NOTHING;
    }
    // 如果是客户端与负载均衡服务器的连接
    if (connection->m_clt_fd == fd) {
        int srv_fd = connection->m_srv_fd;
        switch (type) {
            case READ: {
                RET_CODE res = connection->read_clt();
                switch (res) {
                    case OK: {
                        log(LOG_DEBUG, __FILE__, __LINE__, "content read from client: %s", connection->m_clt_buf);
                    }
                    case BUFFER_FULL: {
                        mod_fd(m_epoll_fd, srv_fd, EPOLLOUT);
                        break;
                    }
                    case IO_ERR:
                    case CLOSED: {
                        free_conn(connection);
                        return CLOSED;
                    }
                    default:
                        break;
                }
                if (connection->m_srv_closed) {
                    free_conn(connection);
                    return CLOSED;
                }
                break;
            }
            case WRITE: {
                RET_CODE res = connection->write_clt();
                switch (res) {
                    case TRY_AGAIN: {
                        mod_fd(m_epoll_fd, fd, EPOLLOUT);
                        break;
                    }
                    case BUFFER_EMPTY: {
                        mod_fd(m_epoll_fd, srv_fd, EPOLLIN);
                        mod_fd(m_epoll_fd, fd, EPOLLIN);
                        break;
                    }
                    case IO_ERR:
                    case CLOSED: {
                        free_conn(connection);
                        return CLOSED;
                    }
                    default:
                        break;
                }
                if (connection->m_srv_closed) {
                    free_conn(connection);
                    return CLOSED;
                }
                break;
            }
            default: {
                log(LOG_ERR, __FILE__, __LINE__, "%s", "other operation not support yet");
                break;
            }
        }
    // 如果是负载均衡服务器与服务端的连接
    } else if (connection->m_srv_fd == fd) {
        int clt_fd = connection->m_clt_fd;
        switch (type) {
            case READ: {
                RET_CODE res = connection->read_srv();
                switch (res) {
                    case OK: {
                        log(LOG_DEBUG, __FILE__, __LINE__, "content read from server: %s", connection->m_srv_buf);
                    }
                    case BUFFER_FULL: {
                        mod_fd(m_epoll_fd, clt_fd, EPOLLOUT);
                        break;
                    }
                    case IO_ERR:
                    case CLOSED: {
                        mod_fd(m_epoll_fd, clt_fd, EPOLLOUT);
                        connection->m_srv_closed = true;
                        break;
                    }
                    default:
                        break;
                }
                break;
            }
            case WRITE: {
                RET_CODE res = connection->write_srv();
                switch (res) {
                    case TRY_AGAIN: {
                        mod_fd(m_epoll_fd, fd, EPOLLOUT);
                        break;
                    }
                    case BUFFER_EMPTY: {
                        mod_fd(m_epoll_fd, clt_fd, EPOLLIN);
                        mod_fd(m_epoll_fd, fd, EPOLLIN);
                        break;
                    }
                    case IO_ERR:
                    case CLOSED: {
                        mod_fd(m_epoll_fd, clt_fd, EPOLLOUT);
                        connection->m_srv_closed = true;
                        break;
                    }
                    default:
                        break;
                }
                break;
            }
            default: {
                log(LOG_ERR, __FILE__, __LINE__, "%s", "other operation not support yet");
                break;
            }
        }
    } else {
        return NOTHING;
    }
    return OK;
}
