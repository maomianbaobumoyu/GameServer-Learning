#ifndef EPOLL_MODE_STRATEGY_H
#define EPOLL_MODE_STRATEGY_H
#include <sys/epoll.h>
#include <memory>

/**
 * @brief Epoll模式策略接口（策略模式）
 */
class EpollModeStrategy {
public:
    virtual ~EpollModeStrategy() = default;
    // 获取当前模式的事件标志
    virtual uint32_t getEvents(uint32_t baseEvents) const = 0;
};

// ET边缘触发模式策略
class ETModeStrategy : public EpollModeStrategy {
public:
    uint32_t getEvents(uint32_t baseEvents) const override {
        return baseEvents | EPOLLET;
    }
};

// LT水平触发模式策略
class LTModeStrategy : public EpollModeStrategy {
public:
    uint32_t getEvents(uint32_t baseEvents) const override {
        return baseEvents;
    }
};
#endif