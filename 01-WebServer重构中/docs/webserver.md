#### webserver的一些私有成员

```c++
    //---------------服务器配制
    int port_;        //服务器监听端口
    bool openLinger_; //是否启用优雅关闭
    int timeoutMS_;   //连接超时时间
    bool isClose_;    //服务器关闭标志
    int listenFd_;    //监听套接字文件描述符
    char *srcDir_;    //静态资源目录路径

    //--------------Epoll事件标志

    //uint32_t是因为它是Linux 内核定义的事件标志的标准类型，并且无符号整数是位操作的唯一安全选择
    uint32_t listenEvent_; //监听套接字的epoll事件标志
    uint32_t connEvent_;   //连接套接字的epoll事件标志

    //----------------子系统组件

    std::unique_ptr<HeapTimer> timer_;        //定时器
    std::unique_ptr<ThreadPool> threadpool_;  //线程池
    std::unique_ptr<Epoller> epoller_;        //Epoll封装
    std::unordered_map<int, HttpConn> users_; //客户端链接映射表
```

#### 为什么要使用unique_ptr来管理？

##### 最核心：RAII 自动资源管理，杜绝泄漏

这三个组件都是**持有系统资源 / 动态内存的大对象**，它们的析构函数有**必须执行的清理工作**：

- `HeapTimer`：持有小根堆动态数组，析构要释放堆内存
- `ThreadPool`：持有多个工作线程和任务队列，析构要停止线程、回收资源
- `Epoller`：持有 epoll 文件描述符，析构要 `close(epollFd_)`

##### 异常安全：构造失败自动回滚

假设：

1. `timer_` 构造成功
2. `threadpool_` 构造成功
3. `epoller_` 构造时抛出异常（比如 `epoll_create` 失败）

使用裸指针，异常抛出WebServer构造终止，timer_ 和 threadpool_已经分配的内存永远泄漏。

使用unique_ptr，已经构造完成的 timer_ 和 threadpool_ 会自动析构，所有资源完全释放，没有任何泄漏。

##### 禁止拷贝，避免大对象拷贝开销

**拷贝 WebServer 会触发这三个大对象的深拷贝**，开销极大（拷贝上万个节点、线程）

语义上完全错误：一个服务器的定时器、线程池、epoll 绝对不能被拷贝

`unique_ptr` **禁止拷贝，只能移动**：

- 编译器会自动禁止 `WebServer` 的拷贝构造和拷贝赋值
- 即使不小心写了 `WebServer srv2 = srv1;`，编译器会直接报错
- 完美符合这三个组件「不可拷贝、只能属于一个 WebServer」的语义

##### 明确「独占所有权」语义

这个 `HeapTimer` 对象**完全归当前 WebServer 所有**

WebServer 负责它的创建、使用和销毁

没有任何其他类会共享这个对象的所有权

------

通过在main函数中通过设置参数在构造函数中使用各种参数初始化服务器。

```c++
//初始化服务器的所有子系统和配制

WebServer::WebServer(int port, int trigMode, int timeoutMS, bool OptLinger,
                     int sqlPort, const char *sqlUser, const char *sqlPwd,
                     const char *dbName, int connPoolNum, int threadNum,
                     bool openLog, int logLevel, int logQueSize) : port_(port), openLinger_(OptLinger), timeoutMS_(timeoutMS), isClose_(false),
                                                                   timer_(new HeapTimer()), threadpool_(new ThreadPool(threadNum)), epoller_(new Epoller())
{
    //srcDir_ 获取当前工作目录，追加/resources/作为静态资源根目录
    //获取进程的当前工作目录 (绝对路径)
    //不手动传缓冲区，路径最大长度限制为 256 字符，系统返回一个自动分配好内存的字符串指针
    srcDir_ = getcwd(nullptr, 256);
    assert(srcDir_);
    strncat(srcDir_, "/resources/", 16);

    //初始化HTTP链接类的静态成员
    //userCount：当前链接数计数器(原子操作)
    //srcDir:静态资源目录路径(所有链接共享)
    HttpConn::userCount = 0;
    HttpConn::srcDir = srcDir_;

    //初始化数据库连接池(单例模式)
    SqlConnPool::Instance()->Init("localhost", sqlPort, sqlUser, sqlPwd, dbName, connPoolNum);

    //初始化事件触发模式
    InitEventMode_(trigMode);

    //初始化监听套接字
    if (!InitSocket_())
    {
        isClose_ = true;
    }

    //初始化日志系统(如果启用)
    if (openLog)
    {
        Log::Instance()->init(logLevel, "./log", ".log", logQueSize);
        if (isClose_)
        {
            LOG_ERROR("========== Server init error!==========");
        }
        else
        {
            LOG_INFO("========== Server init ==========");
            LOG_INFO("Port:%d, OpenLinger: %s", port_, OptLinger ? "true" : "false");
            LOG_INFO("Listen Mode: %s, OpenConn Mode: %s",
                     (listenEvent_ & EPOLLET ? "ET" : "LT"),
                     (connEvent_ & EPOLLET ? "ET" : "LT"));
            LOG_INFO("LogSys level: %d", logLevel);
            LOG_INFO("srcDir: %s", HttpConn::srcDir);
            LOG_INFO("SqlConnPool num: %d, ThreadPool num: %d", connPoolNum, threadNum);
        }
    }
}
```

一般情况下，不推荐指针new，而是使用make_unique(但C++14才有)。

先看**危险写法**（普通函数内，非初始化列表）：

```c++
// 危险！存在内存泄漏风险
void Func() {
    // 步骤1：new HeapTimer 分配内存
    // 步骤2：构造 HeapTimer 对象
    // 步骤3：unique_ptr 接管指针
    std::unique_ptr<HeapTimer> timer(new HeapTimer()); 
}
```

如果 `new HeapTimer()` 构造时**抛出异常**：

- 内存已经分配
- 但 `unique_ptr` 还没来得及接管
- **内存直接泄漏！**

这就是经典的 **`new` 和 `unique_ptr` 赋值之间的间隙风险**



**为什么这个 WebServer 代码里这么写是安全的**

因为它写在了 **【构造函数成员初始化列表】** 中

```c++
WebServer::WebServer(...) :
    // 成员初始化列表！！！
    timer_(new HeapTimer()), 
    threadpool_(new ThreadPool(threadNum)), 
    epoller_(new Epoller())
{}
```

###### 成员初始化列表的特性：

1. **初始化是原子性的**：`new` 完成 → **立刻**交给 `unique_ptr` 接管，**没有间隙**
2. **异常安全**：如果后面的成员构造抛异常，**前面已经初始化的智能指针会自动释放内存**
3. 这是 C++11 里**唯一能安全用 `new` 初始化 `unique_ptr` 成员**的场景

----

析构函数

```c++
//清理服务器资源
WebServer::~WebServer()
{
    close(listenFd_); //关闭监听套接字
    isClose_ = true;
    free(srcDir_);                        //释放工作目录内存
    SqlConnPool::Instance()->ClosePool(); //关闭数据库连接池
}
```

下面是设置监听fd和连接fd触发模式的函数

```c++
/**
 * @brief 初始化事件触发模式
 *
 * 根据 trigMode 设置不同的 epoll 事件标志组合：
 *
 * trigMode = 0: 全 LT 模式（水平触发）
 *   - 监听套接字: EPOLLRDHUP
 *   - 连接套接字: EPOLLONESHOT | EPOLLRDHUP
 *
 * trigMode = 1: 连接 ET 模式（边沿触发）
 *   - 监听套接字: EPOLLRDHUP
 *   - 连接套接字: EPOLLONESHOT | EPOLLRDHUP | EPOLLET
 *
 * trigMode = 2: 监听 ET 模式
 *   - 监听套接字: EPOLLRDHUP | EPOLLET
 *   - 连接套接字: EPOLLONESHOT | EPOLLRDHUP
 *
 * trigMode = 3: 全 ET 模式（默认）
 *   - 监听套接字: EPOLLRDHUP | EPOLLET
 *   - 连接套接字: EPOLLONESHOT | EPOLLRDHUP | EPOLLET
 *
 * @param trigMode 触发模式（0-3）
 */
void WebServer::InitEventMode_(int trigMode)
{
    //基础事件，监听套接字检测对端关闭
    //链接套接字使用ONESHOT+检测对端关闭
    listenEvent_ = EPOLLRDHUP;
    connEvent_ = EPOLLONESHOT | EPOLLRDHUP;

    switch (trigMode)
    {
    case 0:
        //全LT
        break;
    case 1:
        //链接ET
        connEvent_ |= EPOLLET;
        break;
    case 2:
        //监听ET模式
        listenEvent_ |= EPOLLET;
        break;
    case 3:
        //全ET
        listenEvent_ |= EPOLLET;
        connEvent_ |= EPOLLET;
        break;
    default:
        //默认全ET
        listenEvent_ |= EPOLLET;
        connEvent_ |= EPOLLET;
    }

    //设置HTTP连接类的ET模式标志
    //影响read/write操作的读取/写入行为
    HttpConn::isET = (connEvent_ & EPOLLET);
}

```

这里监听fd和连接fd默认都是有EPOLLRDHUP参数

#### `EPOLLRDHUP` 核心含义

**全称**：Epoll Remote Hang Up

**对端（客户端）关闭了连接 / 断开了连接 / TCP 半关闭**

当**客户端**主动调用 `close()` 关闭套接字，或者客户端进程崩溃、网络断开时，Linux 内核会向服务器的 epoll 实例触发 **`EPOLLRDHUP` 事件**，告诉服务器：**对面的客户端已经不发数据了，连接要断了！**

代码中**默认给监听 fd、客户端 fd 都加上了 `EPOLLRDHUP`**。

作用只有一个：**让服务器能第一时间感知「客户端断开连接」**

不管是客户端正常关闭、异常崩溃、断网，服务器 epoll 都会收到这个事件，然后执行：

```c++
else if(events & (EPOLLRDHUP | EPOLLHUP | EPOLLERR)) {
    CloseConn_(&users_[fd]);  // 直接关闭无效连接
}
```

相似的有

|      事件      |               触发时机                |     谁触发     |        用途        |
| :------------: | :-----------------------------------: | :------------: | :----------------: |
| **EPOLLRDHUP** | **客户端关闭写端（正常 / 异常断开）** | 客户端主动断连 | **最常用、最可靠** |
|  **EPOLLHUP**  |    连接**完全断开**（双向都关了）     |      内核      |     被动、滞后     |
|  **EPOLLERR**  |             连接发生错误              |    内核错误    |      异常场景      |

网络编程中**必须监听 EPOLLRDHUP**，它是检测客户端断开的**最优先、最可靠**的事件。

还有一种是：

SIGPIPE：**当服务器向一个已经完全关闭的 TCP 连接写数据时**，内核会向服务器进程发送 SIGPIPE 信号。

**默认行为**：**直接终止整个服务器进程**

```bash
1. 客户端调用close()关闭连接
2. 服务器不知道，继续向这个连接写数据
3. 内核收到写请求，发现连接已经断了
4. 内核向服务器发送SIGPIPE信号
5. 服务器进程直接退出！
```

所有传统 C/S 程序第一行都会写：

```c++
signal(SIGPIPE, SIG_IGN); // 忽略SIGPIPE信号，防止服务器被杀死
```

而EPOLLRDHUP：epoll 专属的 "客户端关闭通知事件"

**当客户端关闭 TCP 连接的写端时**（包括正常 close ()、进程崩溃、网络断开），内核会**主动**向 epoll 实例发送 EPOLLRDHUP 事件。

**处理方式**：服务器在主事件循环中**同步**处理这个事件，关闭对应的连接。

|      对比维度      |                      SIGPIPE                       |               EPOLLRDHUP               |
| :----------------: | :------------------------------------------------: | :------------------------------------: |
|    **触发时机**    |   服务器向已关闭的连接**写数据时**才触发（被动）   |   客户端**刚关闭写端**就触发（主动）   |
|    **感知延迟**    |  极长：可能几小时甚至几天（直到服务器下次写数据）  |      毫秒级：客户端断开后立刻收到      |
|    **处理方式**    |           异步信号处理（会中断任何代码）           |   同步事件处理（在主循环中顺序执行）   |
|    **线程安全**    |   极不安全：信号会随机中断任何线程，导致竞态条件   |   完全安全：所有逻辑在主线程同步执行   |
| **对服务器的影响** |   默认杀死整个进程；即使忽略，也只能知道写失败了   | 只关闭对应的客户端连接，不影响其他连接 |
|    **资源占用**    | 无效连接会一直占用文件描述符和内存，直到写操作发生 |    无效连接立刻被清理，资源及时释放    |
|     **可靠性**     |   不可靠：如果服务器不写数据，永远不知道连接断了   |    100% 可靠：客户端一断就立刻知道     |
|    **性能开销**    |           高：有信号中断、上下文切换开销           |     零开销：和普通 epoll 事件一样      |
|    **适用场景**    |          只能用来**忽略**，防止服务器崩溃          |     专门用来**检测客户端关闭连接**     |

----

下面是服务器事件循环

```c++
/**
 * @brief 启动服务器事件循环
 *
 * 主循环逻辑：
 * 1. 计算 epoll_wait 超时时间（基于定时器）
 * 2. 调用 epoll_wait 等待事件
 * 3. 遍历所有事件，根据类型分发处理
 */
void WebServer::Start()
{
    int timeMS = -1; //无时间将阻塞
    if (!isClose_)
    {
        LOG_INFO("========== Server start ==========");
    }

    while (!isClose_)
    {
        //如果使用了超时检测，获取最近的定时器的超时时间
        if (timeoutMS_ > 0)
        {
            timeMS = timer_->GetNextTick();
        }

        //等待epoll时间，timeMS为超时时间
        int eventCnt = epoller_->Wait(timeMS);

        //遍历所有就绪事件
        for (int i = 0; i < eventCnt; i++)
        {
            //获取事件对应的文件描述符和事件类型
            int fd = epoller_->GetEventFd(i);
            uint32_t events = epoller_->GetEventFd(i);

            if (fd == listenFd_)
            {
                //监听套接字就绪：有新链接
                DealListen_();
            }
            else if (events & (EPOLLRDHUP | EPOLLHUP | EPOLLERR))
            {
                //链接套接字错误/挂起/关闭：关闭客户端链接
                assert(users_.count(fd) > 0);
                CloseConn_(&users_[fd]);
            }
            else if (events & EPOLLIN)
            {
                //连接套接字读事件：读取客户端请求
                assert(users_.count(fd) > 0);
                DealRead_(&users_[fd]);
            }
            else if (events & EPOLLOUT)
            {
                //链接套接字可写：发送响应数据
                assert(users_.count(fd) > 0);
                DealWrite_(&users_[fd]);
            }
            else
            {
                LOG_ERROR("Unexpected event");
            }
        }
    }
}
```

#### epoll，timer联动机制

主要讲下这里的epoll和定时器的联动操作，在这里先检查是否使用了超时检测(默认为-1，永久阻塞)，调用定时器的GetNextTick()函数，在这个函数内部会先调用tick()处理超时的timer，然后计算堆顶的剩余expire时间并返回。

这就是最近的超时时间，通过epoll的Wait()函数，在内部epoll_wait(...,timeMS)阻塞等待这个超时时间，如果超时自动返回0，eventCnt为0，进入下轮循环在GetNextTick()中处理超时timer。

 `epoll_wait` 的返回值规则：

| 返回值 |                          含义                           |
| :----: | :-----------------------------------------------------: |
| `> 0`  |  有 `n` 个文件描述符有事件就绪（新连接、读、写、断开）  |
| `= 0`  | **超时了**，在指定的 `timeoutMs` 毫秒内没有任何事件发生 |
| `< 0`  |               发生错误（比如被信号中断）                |

如果正常返回，触发堆顶的某个事件正常处理并延长超时时间。如果触发的不是堆顶，并且堆顶在执行其他处理语句过程中超时，也留到下次循环的GetNextTick()中处理。

-----

向客户端发送错误信息函数和关闭连接函数

```c++
/**
 * @brief 向客户端发送错误信息并关闭连接
 *
 * 用于服务器繁忙或发生错误时，通知客户端后立即关闭连接
 *
 * @param fd 客户端套接字文件描述符
 * @param info 错误信息字符串
 */
void WebServer::SendError_(int fd, const char *info)
{
    assert(fd > 0);
    int ret = send(fd, info, strlen(info), 0);
    if (ret < 0)
    {
        LOG_WARN("send error to client[%d] error!", fd);
    }
    close(fd);
}

/**
 * @brief 关闭客户端连接
 *
 * 清理客户端相关资源：
 * 1. 从 epoll 移除监听
 * 2. 关闭套接字（HttpConn::Close 会减少连接计数）
 *
 * @param client 指向 HttpConn 对象的指针
 */
void WebServer::CloseConn_(HttpConn *client)
{
    assert(client);
    LOG_INFO("Client[%d] quit!", client->GetFd());
    epoller_->DelFd(client->GetFd());
    client->Close();
}
```

`SendError_`：处理**刚 accept、还没加入 epoll 的临时 fd**，只关闭fd，在DealListen_()中处理服务器连接数已满，拒绝新链接的情况。

`CloseConn_`：处理**已经加入 epoll、完成初始化的正式连接 fd**，先删除epoll映射再关闭fd。

----

添加新客户端连接

```c++
/**
 * @brief 添加新客户端连接
 *
 * 新连接处理流程：
 * 1. 初始化 HttpConn 对象
 * 2. 如果启用了超时检测，添加定时器
 * 3. 注册到 epoll（监听可读事件）
 * 4. 设置为非阻塞模式
 *
 * @param fd 客户端套接字文件描述符
 * @param addr 客户端地址信息
 */
void WebServer::AddClient_(int fd, sockaddr_in addr)
{
    assert(fd > 0);

    // 初始化 HttpConn 对象
    users_[fd].init(fd, addr);

    // 如果启用了超时检测，添加定时器
    // 定时器超时时会调用 CloseConn_ 关闭连接
    if (timeoutMS_ > 0)
    {
        timer_->add(fd, timeoutMS_, std::bind(&WebServer::CloseConn_, this, &users_[fd]));
    }

    //注册到epoll,监听可读事件和连接事件
    epoller_->AddFd(fd, EPOLLIN | connEvent_);
    //设置为非阻塞模式(ET模式必须配合非阻塞使用)
    SetFdNonblock(fd);

    LOG_INFO("Client[%d] in!", users_[fd].GetFd());
}

```

在这里向定时器添加回调函数时使用了bind关键字。

#### 为什么必须用 `bind`

`std::bind` 是 **C++11 专门用来「打包函数」的工具**

把一个**需要参数 / 需要对象才能调用的函数**，打包成一个**不用传参、直接就能调用**的「可执行任务」，存起来**延迟执行**。

定时器的 `add` 函数需要一个 **「超时后自动执行的回调任务」**

这个任务就是：**关闭对应的客户端连接** → `CloseConn_`

其中：

`CloseConn_` 是 `WebServer` 的**成员函数**，它有两个致命限制：

1. **必须用对象调用**：不能直接写 `CloseConn_()`，必须 `对象.CloseConn_()`
2. **必须传参数**：`CloseConn_(HttpConn* client)`

但定时器需要的是：

**一个啥都不用传、直接调用的函数**（超时了直接跑）

`std::bind` 就是解决这个矛盾的！

```c++
std::bind(
    &WebServer::CloseConn_,   // 1. 要绑定的【成员函数】
    this,                    // 2. 用哪个【对象】调用这个函数
    &users_[fd]               // 3. 要传给函数的【参数】
)
```

&WebServer::CloseConn_  ：

- 要绑定的**目标函数**
- 成员函数必须加 `&` 取地址，不能直接写函数名

this ：

- 代表**当前这个 WebServer 实例**
- 意思是：**超时后，用当前服务器对象来调用 CloseConn_**
- 解决了「成员函数必须有对象才能调用」的问题
- 用来选择第一个参数是哪个对象的函数(因为这个是在当前类的函数实现里绑定的，this表示当前类的函数成员CloseConn_)

 &users_[fd] ：

- 传给 `CloseConn_` 的**实参**
- `CloseConn_` 需要一个 `HttpConn*` 指针，这里正好传对应客户端的指针
- 解决了「函数需要传参」的问题
- 参数可以传任意多个,函数有几个形参，bind 就跟几个实参

`bind` 相当于： 把「函数 + 对象 + 参数」提前打包成一个「快递包裹」

```c++
包裹内容 = 调用 this->CloseConn_(&users_[fd])
```

小例子:

```c++
#include <iostream>
#include <functional> // bind 必须的头文件

class WebServer {
public:
    // 要绑定的成员函数
    void CloseConn(int fd) {
        std::cout << "关闭客户端：" << fd << std::endl;
    }

    void test() {
        // 用 bind 打包函数
        auto task = std::bind(&WebServer::CloseConn, this, 100);
        
        // 直接执行打包好的任务，不用传任何参数！
        task(); 
    }
};

int main() {
    WebServer s;
    s.test(); // 输出：关闭客户端：100
}
```

回到WebServer::AddClient_里，这里的bind相当于把原函数打包成了一个无参数，可直接执行的任务函数 void() ,定时器那边使用function< void() >类型的变量(TimeoutCallBack) 回调函数cb来接收。

`std::function` + `std::bind` 是**天生一对**

1. **线程池 / 定时器的任务队列/回调函数**：只认 **`void()` 类型**（无参、无返回值的可调用对象）
2. **`std::bind`**：专门把**任意有参函数、成员函数**，**强行转换成 `void()` 类型**
3. 两者配合，就能让**任何任务**都能塞进统一的任务队列/回调函数里！

----

处理监听套接字事件

```c++
/**
 * @brief 处理监听套接字事件
 *
 * 接受新连接并添加到客户端列表
 *
 * ET 模式下需要循环 accept 直到返回 EAGAIN，
 * 确保一次性处理所有待接受的连接
 */
void WebServer::DealListen_()
{
    struct sockaddr_in addr;
    socketlen_t len = sizeof(addr);
    do
    {
        //接受新链接
        int fd = accept(listenFd_, (struct sockaddr *)&addr, &len);
        if (fd <= 0)
        {
            return; //没有更多链接可以接收
        }
        else if (HttpConn::userCount >= MAX_FD)
        {
            //超过了最大连接数
            SendError_(fd, "Server busy!");
            LOG_WARN("Clients is full!");
            return;
        }

        //添加新客户端
        AddClient_(fd, addr);
    } while (listenEvent_ & EPOLLET); //ET模式下继续循环
}
```

如果把trigMode设为监听ET模式，就会一直循环。

----

处理读事件

```c++
/**
 * @brief 处理可读事件
 *
 * 将读任务添加到线程池执行
 *
 * 为什么要在单独的线程中执行？
 * - read 操作可能阻塞（虽然是非阻塞套接字，但数据可能分多次到达）
 * - 避免阻塞主事件循环
 *
 * @param client 指向 HttpConn 对象的指针
 */
void WebServer::DealRead_(HttpConn *client)
{
    assert(client);
    //延长超时时间
    ExtentTime_(client);
    //将读任务添加到线程池
    threadpool_->AddTask(std::bind(&WebServer::OnRead_, this, client));
}

/**
 * @brief 处理读事件（在线程池中执行）
 *
 * 读事件处理流程：
 * 1. 从套接字读取数据到缓冲区
 * 2. 如果读取错误（非 EAGAIN），关闭连接
 * 3. 否则，处理 HTTP 请求
 *
 * @param client 指向 HttpConn 对象的指针
 */
void WebServer::OnRead_(HttpConn *client)
{
    assert(client);
    int ret = -1;
    int readErrno = 0;

    //读取数据到缓冲区
    ret = client->read(&readErrno);

    //如果读取错误(非EAGAIN)，关闭连接
    if (ret <= 0 && readErrno != EAGAIN)
    {
        CloseConn_(client);
        return;
    }

    //处理HTTP请求
    OnProcess(client);
}
```

在主线程里调用DealRead_函数，延长fd超时时间，并将读事件添加到线程池，读完数据后，调用onProcess根据process注册epoll对应事件。

----

处理写事件函数

```c++
/**
 * @brief 处理可写事件
 *
 * 将写任务添加到线程池执行
 *
 * @param client 指向 HttpConn 对象的指针
 */
void WebServer::DealWrite_(HttpConn* client) {
    assert(client);

    // 延长超时时间（重置定时器）
    ExtentTime_(client);

    // 将写任务添加到线程池
    threadpool_->AddTask(std::bind(&WebServer::OnWrite_, this, client));
}

/**
 * @brief 处理写事件（在线程池中执行）
 *
 * 写事件处理流程：
 * 1. 将缓冲区的响应数据发送到套接字
 * 2. 如果传输完成：
 *    - 如果是 keep-alive 连接：继续处理下一个请求
 *    - 否则：关闭连接
 * 3. 如果写入错误 EAGAIN：注册写事件继续发送
 *
 * @param client 指向 HttpConn 对象的指针
 */
void WebServer::OnWrite_(HttpConn *client)
{
    assert(client);
    int ret = -1;
    int writeErrno = 0;

    //发送响应数据
    ret = client->write(&writeErrno);

    if (client->ToWriteBytes() == 0)
    {
        if (client->IsKeepAlive())
        {
            //keep-alive，继续处理下一个请求
            OnProcess(client);
            return;
        }
    }
    else if (ret < 0)
    {
        if (writeErrno == EAGAIN)
        {
            //EAGAIN 资源暂时不可用，继续传输
            epoller_->ModFd(client->GetFd(), connEvent_ | EPOLLOUT);
            return;
        }
    }

    //传输完成或非keep-alive链接，关闭连接
    CloseConn_(client);
}
```

在OnWrite_ 将响应数据发送给客户端fd，检查客户端是否设置keep-alive标志，如果设置keep-alive标志调用OnProcess注册对应事件。

----

延长连接超时时间和处理HTTP请求函数

```c++
/**
 * @brief 延长连接超时时间
 *
 * 在每次 IO 操作时调用，重置定时器的超时时间
 * 这样可以确保活跃连接不会被误关闭
 *
 * @param client 指向 HttpConn 对象的指针
 */
void WebServer::ExtentTime_(HttpConn *client)
{
    assert(client);
    if (timeoutMS_ > 0)
    {
        timer_->adjust(client->GetFd(), timeoutMS_);
    }
}

/**
 * @brief 处理 HTTP 请求
 *
 * 根据请求解析结果，准备响应并注册相应的事件：
 * - 如果 process() 返回 true：请求已解析完成，注册写事件发送响应
 * - 如果 process() 返回 false：请求未完成，注册读事件继续读取
 *
 * @param client 指向 HttpConn 对象的指针
 */
void WebServer::OnProcess(HttpConn *client)
{
    // 调用HttpConn的process方法，完成请求解析和响应生成
    if(client->process()) {
        // 返回true：请求已完整解析，响应已生成完毕
        // 告诉epoll：现在要监听这个fd的可写事件，准备发送响应
        epoller_->ModFd(client->GetFd(), connEvent_ | EPOLLOUT);
    } else {
        // 返回false：请求不完整（比如数据分多次到达）
        // 告诉epoll：继续监听可读事件，等待客户端发送剩余数据
        epoller_->ModFd(client->GetFd(), connEvent_ | EPOLLIN);
    }
}
```

这里Onprocess是整个HTTP请求处理的调度中心，连接了「数据读取」和「响应发送」两个阶段。

 读取完客户端数据后，交给`OnProcess`解析请求、生成响应，然后告诉 epoll 下一步该监听读事件还是写事件。

**`client->process()` 做了什么？**

**解析请求行**：提取请求方法（GET/POST）、URL、HTTP 版本

**解析请求头**：提取`Connection`、`Content-Length`、`Host`等字段

**解析请求体**：如果是 POST 请求，读取请求体数据

**生成响应**：根据请求的 URL，读取对应的静态资源（html/css/js/ 图片），或者处理业务逻辑

**填充写缓冲区**：把生成的 HTTP 响应（状态行 + 响应头 + 响应体）写入 HttpConn 的写缓冲区

返回true表示已经处理完缓冲区数据，可注册EPOLLOUT事件发送响应了

返回false表示请求不完整/只到了一部分，重新注册EPOLLIN事件

----

初始化监听套接字和设置非阻塞函数

```c++
/**
 * @brief 初始化监听套接字
 *
 * 初始化流程：
 * 1. 验证端口号合法性
 * 2. 创建 TCP 套接字
 * 3. 设置套接字选项（SO_LINGER, SO_REUSEADDR）
 * 4. 绑定地址和端口
 * 5. 开始监听
 * 6. 添加到 epoll
 * 7. 设置为非阻塞模式
 *
 * @return bool 初始化是否成功
 */
/* Create listenFd */
bool WebServer::InitSocket_()
{
    int ret;
    struct sockaddr_in addr;

    //验证端口合法
    if (port_ > 65535 || port_ < 1024)
    {
        LOG_ERROR("Port:%d error", port_);
        return false;
    }

    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htons(INADDR_ANY); //监听所有接口
    addr.sin_port = htons(port_);

    //优雅关闭
    struct linger optLinger = {0};
    if (openLinger_)
    {
        //直到所剩数据发送完毕或超时
        optLinger.l_onoff = 1;  //启用linger
        optLinger.l_linger = 1; //超时时间
    }

    //创建TCP套接字
    listenFd_ = socket(AF_INET, SOCK_STREAM, 0);
    if (listenFd_ < 0)
    {
        LOG_ERROR("Create socket error!", port_);
        return false;
    }

    ret = setsockopt(listenFd_, SOL_SOCKET, SO_LINGER, &optLinger, sizeof(optLinger));
    if (ret < 0)
    {
        close(listenFd_);
        LOG_ERROR("Init linger error!", port_);
        return false;
    }

    //端口重用  运行绑定处于TIME_WAIT状态的地址
    int optval = 1;
    ret = setsockopt(listenFd_, SOL_SOCKET, SO_REUSEADDR, (const void *)&optval, sizeof(int));
    if (ret == -1)
    {
        LOG_ERROR("set socket setsockopt error !");
        close(listenFd_);
        return false;
    }

    //绑定地址和端口
    ret = bind(listenFd_, (struct sockaddr *)&addr, sizeof(addr));
    if (ret < 0)
    {
        LOG_ERROR("Bind Port:%d error!", port_);
        close(listenFd_);
        return false;
    }

    //开始监听
    ret = listen(listenFd_, 128);
    if (ret < 0)
    {
        LOG_ERROR("Listen port:%d error!", port_);
        close(listenFd_);
        return false;
    }

    //添加到epoll
    ret = epoller_->AddFd(listenFd_,listenEvent_|EPOLLIN);
    if(ret == 0) {
        LOG_ERROR("Add listen error!");
        close(listenFd_);
        return false;
    }    
}

/**
 * @brief 设置文件描述符为非阻塞模式
 *
 * 使用 fcntl 获取当前标志，添加 O_NONBLOCK 标志
 * ET 模式必须配合非阻塞套接字使用
 *
 * @param fd 文件描述符
 * @return int 新的文件描述符标志
 */
int WebServer::SetFdNonblock(int fd){
    assert(fd>0);
    return fcntl(fd,F_SETFL,fcntl(fd,F_GETFD,0)|O_NONBLOCK);
}
```

