#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <cassert>
#include <unistd.h>
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <cstdlib>
#include <sys/stat.h>
#include <vector>

#include "log.h"
#include "conn.h"
#include "mgr.h"
#include "processpool.h"

using std::vector;

static const char *version = "1.0";

/**
 * 编译：
 * 运行：./springsnail -f config.xml
 * 函数功能：首先分析命令行参数，打开配置文件，之后使用有限状态机读取配置参数，接着构造 processpool 对象后启动，使用了单例模式
 * 注意：.xml 文件格式有错误，而且使用有限状态机读取配置的方式有限制
 * */
int main(int argc, char *argv[]) {
    char cfg_file[1024]; // 配置文件路径 config.xml
    memset(cfg_file, '\0', 1024);
    int option;
    while ((option = getopt(argc, argv, "f:xvh")) != -1) {
        switch (option) {
            case 'x': {
                set_loglevel(LOG_DEBUG);
                break;
            }
            case 'v': {
                log(LOG_INFO, __FILE__, __LINE__, "%s %s", argv[0], version);
                return 0;
            }
            case 'h': {
                log(LOG_INFO, __FILE__, __LINE__,
                    "usage: %s [-h] [-v] [-f config_file]", basename(argv[0]));
                return 0;
            }
            case 'f': {
                memcpy(cfg_file, optarg, strlen(optarg));
                break;
            }
            case '?': {
                log(LOG_ERR, __FILE__, __LINE__, "un-recognized option %c", option);
                log(LOG_INFO, __FILE__, __LINE__,
                    "usage: %s [-h] [-v] [-f config_file]", basename(argv[0]));
                return 1;
            }
            default: {
            }
        }
    }

    if (cfg_file[0] == '\0') {
        log(LOG_ERR, __FILE__, __LINE__, "%s", "please specify the config file");
        return 1;
    }
    int cfg_fd = open(cfg_file, O_RDONLY);
    if (!cfg_fd) {
        log(LOG_ERR, __FILE__, __LINE__, "read config file met error: %s", strerror(errno));
        return 1;
    }
    struct stat ret_stat{};
    if (fstat(cfg_fd, &ret_stat) < 0) {
        log(LOG_ERR, __FILE__, __LINE__, "read config file met error: %s", strerror(errno));
        return 1;
    }
    char *buf = new char[ret_stat.st_size + 1];
    memset(buf, '\0', ret_stat.st_size + 1);
    ssize_t read_sz = read(cfg_fd, buf, ret_stat.st_size);
    if (read_sz < 0) {
        log(LOG_ERR, __FILE__, __LINE__, "read config file met error: %s", strerror(errno));
        return 1;
    }

    vector<host> balance_srv; // 负载均衡服务器信息
    vector<host> logical_srv; // 逻辑服务器信息
    host tmp_host{};
    memset(tmp_host.m_hostname, '\0', 1024);
    char *tmp_hostname;
    char *tmp_port;
    char *tmp_conn_cnt;
    bool open_tag = false;
    char *tmp = buf;
    char *tmp2 = nullptr;
    char *tmp3 = nullptr;
    char *tmp4 = nullptr;
    while (tmp2 = strpbrk(tmp, "\n")) {
        *tmp2++ = '\0';
        if (strstr(tmp, "<logical_host>")) {
            if (open_tag) {
                log(LOG_ERR, __FILE__, __LINE__, "%s", "parse config file failed");
                return 1;
            }
            open_tag = true;
        } else if (strstr(tmp, "</logical_host>")) {
            if (!open_tag) {
                log(LOG_ERR, __FILE__, __LINE__, "%s", "parse config file failed");
                return 1;
            }
            logical_srv.push_back(tmp_host);
            memset(tmp_host.m_hostname, '\0', 1024);
            open_tag = false;
        } else if (tmp3 = strstr(tmp, "<name>")) {
            tmp_hostname = tmp3 + 6;
            tmp4 = strstr(tmp_hostname, "</name>");
            if (!tmp4) {
                log(LOG_ERR, __FILE__, __LINE__, "%s", "parse config file failed");
                return 1;
            }
            *tmp4 = '\0';
            memcpy(tmp_host.m_hostname, tmp_hostname, strlen(tmp_hostname));
        } else if (tmp3 = strstr(tmp, "<port>")) {
            tmp_port = tmp3 + 6;
            tmp4 = strstr(tmp_port, "</port>");
            if (!tmp4) {
                log(LOG_ERR, __FILE__, __LINE__, "%s", "parse config file failed");
                return 1;
            }
            *tmp4 = '\0';
            tmp_host.m_port = atoi(tmp_port);
        } else if (tmp3 = strstr(tmp, "<conns>")) {
            tmp_conn_cnt = tmp3 + 7;
            tmp4 = strstr(tmp_conn_cnt, "</conns>");
            if (!tmp4) {
                log(LOG_ERR, __FILE__, __LINE__, "%s", "parse config file failed");
                return 1;
            }
            *tmp4 = '\0';
            tmp_host.m_conn_cnt = atoi(tmp_conn_cnt);
        } else if (tmp3 = strstr(tmp, "Listen")) {
            tmp_hostname = tmp3 + 6;
            tmp4 = strstr(tmp_hostname, ":");
            if (!tmp4) {
                log(LOG_ERR, __FILE__, __LINE__, "%s", "parse config file failed");
                return 1;
            }
            *tmp4++ = '\0';
            tmp_host.m_port = atoi(tmp4);
            memcpy(tmp_host.m_hostname, tmp3, strlen(tmp3));
            balance_srv.push_back(tmp_host);
            memset(tmp_host.m_hostname, '\0', 1024);
        }
        tmp = tmp2;
    }

    if (balance_srv.empty() || logical_srv.empty()) {
        log(LOG_ERR, __FILE__, __LINE__, "%s", "parse config file failed");
        return 1;
    }
    const char *ip = balance_srv[0].m_hostname;
    int port = balance_srv[0].m_port;

    int listen_fd = socket(PF_INET, SOCK_STREAM, 0);
    assert(listen_fd >= 0);

    int ret;
    struct sockaddr_in address{};
    bzero(&address, sizeof(address));
    address.sin_family = AF_INET;
    inet_pton(AF_INET, ip, &address.sin_addr);
    address.sin_port = htons(port);

    ret = bind(listen_fd, (struct sockaddr *) &address, sizeof(address));
    assert(ret != -1);

    ret = listen(listen_fd, 5);
    assert(ret != -1);

    processpool<conn, host, mgr> *pool = processpool<conn, host, mgr>::create(listen_fd, logical_srv.size());
    if (pool) {
        pool->run(logical_srv);
        delete pool;
    }

    close(listen_fd);
    return 0;
}
