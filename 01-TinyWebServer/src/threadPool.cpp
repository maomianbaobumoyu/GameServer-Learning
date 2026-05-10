#include "threadPool.h"

// 构造函数：启动指定数量的工作线程
threadPool::threadPool(int threadPoolSize):stop(false) {
    startThreadPool(threadPoolSize);
}

// 析构函数：安全销毁线程池
threadPool::~threadPool() {
    stop = true;
    task_cv.notify_all();
    for(auto &worker : workers) {
        worker.join();
    }
}

// 添加任务到线程池
void threadPool::addTask(std::function<void()> task) {
    {
        std::unique_lock<std::mutex> lock(task_mutex);
        tasks.push(std::move(task));
    }
    task_cv.notify_one();
}

// 启动工作线程
void threadPool::startThreadPool(size_t numThreads) {
    for(size_t i=0; i<numThreads; i++) {
        workers.emplace_back([this] {
            while(true) {
                std::function<void()> task;
                {
                    std::unique_lock<std::mutex> lock(task_mutex);
                    // 等待任务或停止信号，处理虚假唤醒
                    task_cv.wait(lock, [this]{
                        return stop || !tasks.empty();
                    });
                    // 线程池停止且任务为空，退出
                    if(stop && tasks.empty()) return;
                    // 获取任务
                    task = std::move(tasks.front());
                    tasks.pop();
                }
                // 执行任务，捕获异常避免线程崩溃
                try {
                    task();
                } catch (...) {}
            }
        });
    }
}