#ifndef THREADPOOL_H
#define THREADPOOL_H
#include <vector>
#include <thread>
#include <queue>
#include <condition_variable>
#include <mutex>
#include <functional>
#include <cstddef>
#include <memory> 
#include <atomic>

/**
 * @brief 基于C++11的线程池，异步执行HTTP业务处理任务
 */
class threadPool
{
private:
    std::vector<std::thread> workers;  // 工作线程数组
    std::queue<std::function<void()>> tasks;  // 任务队列
    std::mutex task_mutex;  // 任务队列互斥锁
    std::condition_variable task_cv;  // 任务条件变量
    std::atomic<bool> stop{false};  // 线程池停止标志

    // 启动指定数量的工作线程
    void startThreadPool(size_t numThreads);
public:
    // 构造函数：启动指定数量的工作线程
    threadPool(int threadPoolSize);
    // 析构函数：安全销毁线程池
    ~threadPool();

    // 添加任务到线程池
    void addTask(std::function<void()> task);
};
#endif