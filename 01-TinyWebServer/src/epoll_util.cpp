#include "epoll_util.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <sys/epoll.h>
#include <unistd.h>
#include <fcntl.h>
#include <string>
#include <memory>

const char* WWW_ROOT = "./www"; // 静态资源根目录

// 初始化默认ET模式策略
std::unique_ptr<EpollModeStrategy> EpollUtil::modeStrategy = std::unique_ptr<EpollModeStrategy>(new ETModeStrategy());

// 创建epoll句柄
int EpollUtil::createEpoll() {
    int epollFd = epoll_create1(0);
    if (epollFd == -1) {
        perror("epoll_create1 error");
        exit(EXIT_FAILURE);
    }
    return epollFd;
}

// 动态切换ET/LT模式
void EpollUtil::setModeStrategy(std::unique_ptr<EpollModeStrategy> strategy) {
    modeStrategy = std::move(strategy);
}

// 向epoll添加fd并注册事件
void EpollUtil::addFd(int epollFd, int fd, uint32_t events) {
    epoll_event ev;
    memset(&ev,0,sizeof(ev));
    ev.data.fd = fd;
    // 策略模式：动态获取事件标志
    ev.events = modeStrategy->getEvents(events);
    // 添加到epoll
    if (epoll_ctl(epollFd, EPOLL_CTL_ADD, fd, &ev) == -1) {
        perror("epoll_ctl add error");
        close(fd);
    }
    // 设置非阻塞模式
    setNonBlock(fd);
}

// 修改fd的监听事件
void EpollUtil::modFd(int epollFd, int fd, uint32_t events) {
    epoll_event ev;
    memset(&ev,0,sizeof(ev));
    ev.data.fd = fd;
    // 策略模式：动态获取事件标志
    ev.events = modeStrategy->getEvents(events);
    // 修改事件
    if (epoll_ctl(epollFd, EPOLL_CTL_MOD, fd, &ev) == -1) {
        perror("epoll_ctl mod error");
        close(fd);
    }
}

// 从epoll删除fd并关闭连接
void EpollUtil::delFd(int epollFd, int fd) {
    if (epoll_ctl(epollFd, EPOLL_CTL_DEL, fd, nullptr) == -1) {
        perror("epoll_ctl del error");
    }
    close(fd);
}

// 设置fd为非阻塞模式
void EpollUtil::setNonBlock(int fd) {
    int flag = fcntl(fd, F_GETFL);
    if (flag == -1) {
        perror("fcntl F_GETFL error");
        close(fd);
        return;
    }
    if (fcntl(fd, F_SETFL, flag | O_NONBLOCK) == -1) {
        perror("fcntl F_SETFL error");
        close(fd);
    }
}