#ifndef CONN_H
#define CONN_H

/**
 * 这个类主要负责连接好之后对客户端和服务端的读写操作，以及返回服务端的状态
 * */

#include <arpa/inet.h>
#include "fdwrapper.h"

class conn {
public:
    conn();

    ~conn();

    void init_clt(int sock_fd, const sockaddr_in &client_addr); // 初始化客户端地址

    void init_srv(int sock_fd, const sockaddr_in &server_addr); // 初始化服务器端地址

    void reset(); // 重置读写缓冲

    RET_CODE read_clt(); // 客户端接收数据并写入 m_clt_buf，调用 recv

    RET_CODE write_clt(); // 客户端发送数据，调用 send

    RET_CODE read_srv(); // 服务端接收数据并写入 m_clt_buf，调用 recv

    RET_CODE write_srv(); // 服务端发送数据，调用 send

public:
    static const int BUF_SIZE = 2048;

    char *m_clt_buf;  // 客户端文件缓冲区
    int m_clt_read_idx; // 客户端读下标
    int m_clt_write_idx; // 客户端写下标
    sockaddr_in m_clt_address;
    int m_clt_fd;

    char *m_srv_buf;
    int m_srv_read_idx;
    int m_srv_write_idx;
    sockaddr_in m_srv_address;
    int m_srv_fd;

    bool m_srv_closed; //标志（用来标志服务端是否关闭）
};

#endif