#ifndef EPOLL_UTIL_H
#define EPOLL_UTIL_H
#include <sys/epoll.h>
#include <unistd.h>
#include <fcntl.h>
#include <vector>
#include "epoll_mode_strategy.h"

// 静态资源根目录
extern const char* WWW_ROOT;
// epoll单次最大就绪事件数
const int MAX_EVENTS = 1024;

/**
 * @brief Epoll工具类：封装Linux epoll系统调用，Reactor模式核心
 */
class EpollUtil {
public:
    // 创建epoll句柄，返回文件描述符
    static int createEpoll();
    // 运行时动态切换ET/LT模式
    static void setModeStrategy(std::unique_ptr<EpollModeStrategy> strategy);
    
    // 向epoll添加fd并注册监听事件
    static void addFd(int epollFd, int fd, uint32_t events);
    // 修改fd的监听事件
    static void modFd(int epollFd, int fd, uint32_t events);
    // 从epoll删除fd并关闭连接
    static void delFd(int epollFd, int fd);
    // 设置fd为非阻塞模式
    static void setNonBlock(int fd);
private:
    // 当前Epoll模式策略
    static std::unique_ptr<EpollModeStrategy> modeStrategy;
};
#endif