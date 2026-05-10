#include <sys/socket.h>
#include <netinet/in.h>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <vector>
#include <signal.h>
#include <memory> 
#include <string>
#include "epoll_util.h"
#include "threadPool.h"
#include "http_conn.h"

const int PORT = 8080;                  // 服务器监听端口
const int DEFAULT_THREAD_POOL_SIZE = 4; // 默认线程池大小
int THREAD_POOL_SIZE = DEFAULT_THREAD_POOL_SIZE;
const int MAX_FD = 1024;                // 最大连接数

std::vector<std::unique_ptr<HttpConn>> clients; // 连接数组
epoll_event events[MAX_EVENTS];                  // epoll事件数组
bool serverRunning = true;                        // 服务器运行标志

// 信号处理函数：捕获SIGINT/SIGTERM，优雅退出
void sigHandler(int sig) {
    if (sig == SIGINT || sig == SIGTERM) {
        serverRunning = false;
        printf("\nServer is shutting down...\n");
    }
}

/**
 * @brief 主函数：服务器入口
 * 支持命令行参数：./http_server [线程池大小] [ET/LT模式]
 */
int main(int argc, const char* argv[]) {
    int threadPoolSize = DEFAULT_THREAD_POOL_SIZE;
    bool useETMode = true;

    // 解析命令行参数1：线程池大小
    if (argc >= 2) {
        int size = atoi(argv[1]);
        threadPoolSize = (size > 0) ? size : DEFAULT_THREAD_POOL_SIZE;
    }
    // 解析命令行参数2：ET/LT模式
    if (argc >= 3) {
        std::string modeStr = argv[2];
        if (modeStr == "LT" || modeStr == "lt") useETMode = false;
        else if (modeStr == "ET" || modeStr == "et") useETMode = true;
    }

    // 打印启动配置
    printf("========================================\n");
    printf("Server Config:\n");
    printf("  Port:          %d\n", PORT);
    printf("  Thread Pool:   %d\n", threadPoolSize);
    printf("  Epoll Mode:    %s\n", useETMode ? "ET (Edge Triggered)" : "LT (Level Triggered)");
    printf("========================================\n");

    // 忽略SIGPIPE，避免客户端断开导致服务器崩溃
    signal(SIGPIPE, SIG_IGN);
    // 注册优雅退出信号
    signal(SIGINT, sigHandler);
    signal(SIGTERM, sigHandler);

    // 1. 初始化线程池
    threadPool pool(threadPoolSize);
    printf("ThreadPool init success, size: %d\n", threadPoolSize);

    // 2. 创建监听socket
    int listenFd = socket(AF_INET, SOCK_STREAM, 0);
    if (listenFd == -1) {
        perror("socket error");
        exit(EXIT_FAILURE);
    }
    // 设置端口复用
    int opt = 1;
    setsockopt(listenFd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    // 绑定地址和端口
    sockaddr_in serverAddr;
    memset(&serverAddr, 0, sizeof(serverAddr));
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_addr.s_addr = htonl(INADDR_ANY);
    serverAddr.sin_port = htons(PORT);
    if (bind(listenFd, (sockaddr*)&serverAddr, sizeof(serverAddr)) == -1) {
        perror("bind error");
        close(listenFd);
        exit(EXIT_FAILURE);
    }
    // 开始监听
    if (listen(listenFd, 128) == -1) {
        perror("listen error");
        close(listenFd);
        exit(EXIT_FAILURE);
    }
    printf("Server start listening on port %d...\n", PORT);

    // 3. 创建epoll句柄并设置模式
    int epollFd = EpollUtil::createEpoll();
    if (useETMode) {
        EpollUtil::setModeStrategy(std::unique_ptr<EpollModeStrategy>(new ETModeStrategy()));
        printf("Using ET (Edge Triggered) mode\n");
    } else {
        EpollUtil::setModeStrategy(std::unique_ptr<EpollModeStrategy>(new LTModeStrategy()));
        printf("Using LT (Level Triggered) mode\n");
    }
    // 注册监听fd读事件
    EpollUtil::addFd(epollFd, listenFd, EPOLLIN);
    // 初始化连接数组
    clients.resize(MAX_FD);

    // Reactor主事件循环
    while (serverRunning) {
        int nfds = epoll_wait(epollFd, events, MAX_EVENTS, 100);
        if (nfds == -1 && errno != EINTR) {
            perror("epoll_wait error");
            break;
        }

        // 遍历就绪事件
        for (int i = 0; i < nfds; ++i) {
            int fd = events[i].data.fd;

            // 事件1：监听fd读事件 → 新连接
            if (fd == listenFd) {
                sockaddr_in clientAddr;
                socklen_t clientAddrLen = sizeof(clientAddr);
                // ET模式循环accept所有新连接
                while (true) {
                    int connFd = accept4(listenFd, (sockaddr*)&clientAddr, &clientAddrLen, SOCK_NONBLOCK);
                    if (connFd == -1) {
                        if (errno == EAGAIN || errno == EWOULDBLOCK) break;
                        perror("accept4 error");
                        continue;
                    }
                    printf("New client connect: %s:%d\n", inet_ntoa(clientAddr.sin_addr), ntohs(clientAddr.sin_port));
                    // 限制最大连接数
                    if (connFd >= MAX_FD) {
                        close(connFd);
                        continue;
                    }
                    // 初始化连接并注册到epoll
                    clients[connFd] = std::unique_ptr<HttpConn>(new HttpConn());
                    clients[connFd]->init(connFd, clientAddr);
                    EpollUtil::addFd(epollFd, connFd, EPOLLIN | EPOLLONESHOT);
                }
            }
            // 事件2：客户端fd读事件 → 读取请求
            else if (events[i].events & EPOLLIN) {
                HttpConn* conn = clients[fd].get();
                if (!conn) continue;
                // 非阻塞读取数据
                ssize_t len = conn->readData();
                if (len <= 0) {
                    conn->closeConn(epollFd);
                    clients[fd].reset();
                    continue;
                }
                // 提交任务到线程池
                pool.addTask([conn, epollFd, fd]() {
                    conn->processRequest();
                    EpollUtil::modFd(epollFd, fd, EPOLLOUT | EPOLLONESHOT);
                });
            }
            // 事件3：客户端fd写事件 → 发送响应
            else if (events[i].events & EPOLLOUT) {
                HttpConn* conn = clients[fd].get();
                if (!conn) continue;
                // 发送响应
                ssize_t len = conn->sendResponse();
                if (len <= 0) {
                    conn->closeConn(epollFd);
                    clients[fd].reset();
                    continue;
                }
                // 重新注册读事件
                EpollUtil::modFd(epollFd, fd, EPOLLIN | EPOLLONESHOT);
            }
            // 事件4：异常事件 → 关闭连接
            else if (events[i].events & (EPOLLRDHUP | EPOLLHUP | EPOLLERR)) {
                HttpConn* conn = clients[fd].get();
                if (conn) {
                    conn->closeConn(epollFd);
                    clients[fd].reset();
                }
            }
        }
    }

    // 优雅退出：释放资源
    close(listenFd);
    close(epollFd);
    printf("Server shutdown success!\n");
    return 0;
}