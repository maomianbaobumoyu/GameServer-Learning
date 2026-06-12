HttpConn 代表一个客户端连接，负责：

管理连接的生命周期（初始化、读写、关闭）

- 维护读写缓冲区

- 解析 HTTP 请求

- 生成 HTTP 响应
- 每个客户端连接对应一个 HttpConn 对象

### 初始成员

先来看内部的静态成员和私有成员

```c++
    // ==================== 静态成员 ====================

    static bool isET;                  //是否为ET模式
    static const char *srcDir;         //静态资源目录
    static std::atomic<int> userCount; //当前连接数(原子计数)
private:
    // ==================== 连接信息 ====================

    int fd_;                  //套接字文件描述符
    struct sockaddr_in addr_; //客户端地址信息
    bool isClose_;            //链接是否已关闭

    // ==================== IO 向量 ====================

    int iovCnt_;          //iovec数组元素个数(1或2)
    struct iovec iov_[2]; //iov[0]为响应头，iov_[1]为响应体

    // ==================== 缓冲区 ====================

    Buffer readBuff_;  //读缓冲区
    Buffer writeBuff_; //写缓冲区

    // ==================== HTTP 处理 ====================

    HttpRequest request_;   //HTTP请求解析器
    HttpResponse response_; //HTTP响应生成器
```

这里的iovec结构体用来为writev服务

在.cpp文件里初始化静态成员

```c++
// ==================== 静态成员初始化 ====================

const char *HttpConn::srcDir = nullptr;  //静态资源目录
std::atomic<int> HttpConn::userCount(0); //当前连接数(0)
bool HttpConn::isET = false;             //是否为ET模式

```



----

析构函数和构造函数

```c++
//初始化链接状态为关闭
HttpConn::HttpConn()
{
    fd_ = -1; //客户端fd,地址默认无，关闭
    addr_ = {0};
    isClose_ = true;
};

//关闭连接并释放资源
HttpConn::~HttpConn()
{
    Close();
};
```

### 连接管理

```c++
/**
 * @brief 初始化连接
 *
 * 在新连接建立时调用，初始化连接状态
 *
 * @param fd 套接字文件描述符
 * @param addr 客户端地址信息
 */
void HttpConn::init(int fd, const sockaddr_in &addr)
{
    assert(fd > 0);
    userCount++;  //增加链接计数
    addr_ = addr; //保存客户端地址
    //清空读写缓冲区
    writeBuff_.RetrieveAll();
    readBuff_.RetrieveAll();

    isClose_ = false; //标记链接为活跃状态

    LOG_INFO("Client[%d](%s:%d) in, userCount:%d", fd_, GetIP(), GetPort(), (int)userCount);
};

/**
 * @brief 关闭连接
 *
 * 释放内存映射文件，关闭套接字，减少连接计数
 */
void HttpConn::Close()
{
    //释放mmap映射的文件
    response_.UnmapFile();

    if (isClose_ == false)
    {
        isClose_ = true;
        userCount--; //减少链接计数
        close(fd_);  //关闭套接字

        LOG_INFO("Client[%d](%s:%d) quit, UserCount:%d", fd_, GetIP(), GetPort(), (int)userCount);
    }
}
```

在每次有新链接到来时调用Init初始化相关成员，保存其客户端地址，标记为活跃状态。

关闭连接时，取消response_中映射的文件(munmap)，如果还处于活用状态，更新其状态及响应变量。

### 信息获取

```c++
int HttpConn::GetFd() const
{
    return fd_;
}

struct sockaddr_in HttpConn::GetAddr() const
{
    return addr_;
}

const char *HttpConn::GetIP() const
{
    return inet_ntoa(addr_.sin_addr);
}

int HttpConn::GetPort() const
{
    return addr_.sin_port;
}
```

封装对外的接口，不直接暴露内部成员

### 数据读写

```c++
/**
 * @brief 从套接字读取数据到读缓冲区
 *
 * 使用 readv 分散读：
 * - 优先读取到 Buffer 的 writable 区域
 * - 如果 Buffer 空间不足，额外读取到栈上的临时数组
 *
 * ET 模式下需要循环读取直到返回 EAGAIN
 *
 * @param saveErrno 保存 errno 值
 * @return ssize_t 读取的字节数，-1 表示错误
 */
ssize_t HttpConn::read(int *saveErrno)
{
    ssize_t len = -1;
    do
    {
        len = readBuff_.ReadFd(fd_, saveErrno);
        if (len <= 0)
            break;  //读完了或者读出错退出
    } while (isET); //ET模式下继续读取
    return len;
}
```

这里使用Buffer中的分散读函数，使用readv读到缓冲区和栈中，最后返回读取的字节数。

```c++
ssize_t HttpConn::write(int* saveErrno) {
    ssize_t len = -1;
    // 核心：do-while循环，适配ET模式和大文件
    do {
        //这里的writev要用的参数在process函数中已经配制好了
        
        // 步骤1：调用writev一次性发送两段数据
        len = writev(fd_, iov_, iovCnt_);

        // 步骤2：处理发送错误
        if(len <= 0) {
            *saveErrno = errno;
            break;
        }

        // 步骤3：所有数据都发送完成，退出循环
        if(iov_[0].iov_len + iov_[1].iov_len == 0) {
            break;
        }
        // 步骤4：发送了部分数据，分两种情况处理
        else if(static_cast<size_t>(len) > iov_[0].iov_len) {
            // 情况A：响应头已经全部发完，剩下的发的是文件内容
            
            // 计算文件部分发送了多少字节
            size_t file_sent = len - iov_[0].iov_len;
            // 调整文件部分的指针和长度
            iov_[1].iov_base = (uint8_t*)iov_[1].iov_base + file_sent;
            iov_[1].iov_len -= file_sent;
            // 清空响应头缓冲区
            if(iov_[0].iov_len) {
                writeBuff_.RetrieveAll();
                iov_[0].iov_len = 0;
            }
        }
        else {
            // 情况B：只发送了部分响应头，文件内容一点都没发
            // 调整响应头部分的指针和长度
            iov_[0].iov_base = (uint8_t*)iov_[0].iov_base + len;
            iov_[0].iov_len -= len;
            // 移动缓冲区读指针，标记已发送
            writeBuff_.Retrieve(len);
        }
    // 循环条件：ET模式 或者 待发送数据大于10KB
    } while(isET || ToWriteBytes() > 10240);
    
    return len;
}
```

#### 为什么不用 `Buffer::WriteFd`，要用 `writev`？

这是最核心的问题。HTTP 响应的结构是：

```bash
HTTP/1.1 200 OK\r\n
Content-Type: text/html\r\n
Content-Length: 1234\r\n
\r\n
<html>...</html>  <!-- 文件内容 -->
```

- **响应头**：存放在 `writeBuff_` 缓冲区里（连续内存）
- **响应体**：是静态文件的内容，通过 `mmap` 映射到内存（另一块连续内存）

这两块是**完全不连续的内存**！如果用普通的 `write` 系统调用，需要:

```c++
// 两次系统调用，性能差
write(fd, writeBuff_.Peek(), writeBuff_.ReadableBytes()); // 发响应头
write(fd, mmap_addr, file_size); // 发文件内容
```

而 `writev` 聚集写可以**一次系统调用**，把多个不连续的内存块一次性发送出去，减少一半的系统调用开销。

------

#### 核心设计思想

这个函数的设计非常巧妙：

> 用一个 `iovec` 数组描述两段待发送数据：
>
> 1. `iov_[0]`：指向 `writeBuff_` 的可读区域（响应头）
> 2. `iov_[1]`：指向 `mmap` 映射的文件内存（响应体）
>
> 用 `writev` 一次性发送这两段数据，然后根据实际发送的字节数，动态调整 `iovec` 的指针和长度，记录发送进度。
>
> 配合 `do-while` 循环，完美适配 ET 模式和大文件发送。

####  `writev` 系统调用

和 `readv` 分散读对应，`writev` 是  **聚集写（Gather Write）**系统调用：

```c++
#include <sys/uio.h>
ssize_t writev(int fd, const struct iovec *iov, int iovcnt);
```

- 作用：把多个不连续的内存块，按顺序一次性写入到文件描述符
- 返回值：实际成功发送的总字节数
- 特点：原子性，要么全部成功，要么部分成功，不会出现交叉写入

####  `iovec` 结构体

```c++
struct iovec {
    void *iov_base;  // 内存块起始地址
    size_t iov_len;  // 内存块长度
};
```

在这个函数里，`iov_` 是 `HttpConn` 类的成员变量，永远只有 2 个元素：

- `iov_[0]`：永远对应响应头
- `iov_[1]`：永远对应文件内容

#### 循环条件：`do-while(isET || ToWriteBytes() > 10240)`

分两种情况：

- **`isET`（边沿触发模式）**：必须循环写

  > ET 模式下，只有当套接字的可写状态从 "不可写" 变为 "可写" 时，才会触发一次`EPOLLOUT`事件。
  >
  > 如果一次`writev`没写完所有数据，内核不会再触发`EPOLLOUT`，剩下的数据会永远发不出去。
  >
  > 所以必须循环写，直到发完所有数据或者返回`EAGAIN`（发送缓冲区满）。

- **`ToWriteBytes() > 10240`（数据量大于 10KB）**：优化性能

  > 即使是 LT 模式，如果待发送的数据量很大（比如大文件），一次`writev`也发不完。
  >
  > 循环写可以减少系统调用次数，提高发送效率。
  >
  > 10KB 是一个经验值，平衡了系统调用开销和 CPU 占用。

####  情况 A：响应头全部发完，开始发文件

```c++
else if(static_cast<size_t>(len) > iov_[0].iov_len) {
    // 计算文件部分发送了多少字节
    size_t file_sent = len - iov_[0].iov_len;
    // 移动文件指针到已发送位置的下一个字节
    iov_[1].iov_base = (uint8_t*)iov_[1].iov_base + file_sent;
    // 减少文件剩余长度
    iov_[1].iov_len -= file_sent;
    // 响应头已经全部发完，清空缓冲区
    if(iov_[0].iov_len) {
        writeBuff_.RetrieveAll();
        iov_[0].iov_len = 0;
    }
}
```

**例子**：

- 响应头长度：500 字节（`iov_[0].iov_len = 500`）
- 文件长度：2000 字节（`iov_[1].iov_len = 2000`）
- `writev` 实际发送了 1200 字节
- 计算：`1200 > 500` → 响应头全部发完，文件发了`1200-500=700`字节
- 调整后：`iov_[1].iov_len = 2000-700=1300`字节，`iov_[0].iov_len = 0`

#### 情况 B：只发了部分响应头

```c++
else {
    // 移动响应头指针到已发送位置的下一个字节
    iov_[0].iov_base = (uint8_t*)iov_[0].iov_base + len;
    // 减少响应头剩余长度
    iov_[0].iov_len -= len;
    // 移动缓冲区读指针，标记已发送
    writeBuff_.Retrieve(len);
}
```

**例子**：

- 响应头长度：500 字节
- `writev` 实际发送了 200 字节
- 调整后：`iov_[0].iov_len = 500-200=300`字节
- 下一次循环会从第 200 字节开始继续发送响应头

#### 为什么用 `uint8_t*` 做指针偏移？

因为 `void*` 指针不能做算术运算（编译器不知道每个元素的大小，在结构体iovec中iov_base就是void*类型的），必须转换成**字节级别的指针**才能按字节偏移。`uint8_t` 就是无符号字符型，大小为 1 字节，刚好适合做内存偏移。

#### 为什么 `static_cast(len)`？

`len` 是 `ssize_t` 类型（有符号整数），`iov_[0].iov_len` 是 `size_t` 类型（无符号整数）。如果直接比较，当 `len` 为负数时，会被转换成一个很大的无符号数，导致判断错误。用 `static_cast` 强制转换，保证类型安全。

#### `Retrieve` 和 `RetrieveAll` 的作用

- `writeBuff_.Retrieve(len)`：移动缓冲区的读指针 `len` 个字节，标记这部分数据已经发送，不需要再保留
- `writeBuff_.RetrieveAll()`：清空整个缓冲区，响应头全部发完后调用

#### 完整执行流程

假设我们要发送一个响应：

- 响应头：500 字节
- 文件内容：2000 字节
- 总长度：2500 字节
- 模式：ET 模式

**执行步骤：**

1. **初始化 iov 数组**：
   - `iov_[0].iov_base = writeBuff_.Peek()`，`iov_[0].iov_len = 500`
   - `iov_[1].iov_base = mmap_addr`，`iov_[1].iov_len = 2000`
   - `iovCnt_ = 2`
2. **第一次循环**：
   - 调用 `writev`，实际发送了 1200 字节
   - `1200 > 500` → 响应头全部发完，文件发了 700 字节
   - 调整 `iov_[1].iov_len = 1300`，`iov_[0].iov_len = 0`
   - 清空 `writeBuff_`
3. **第二次循环**：
   - 调用 `writev`，实际发送了 1300 字节
   - `iov_[0].iov_len + iov_[1].iov_len = 0` → 所有数据发完
   - 退出循环
4. **返回结果**：返回 1300 字节，上层判断所有数据发送完成，处理长连接或关闭连接。

------

#### 错误处理

当 `writev` 返回 `len <= 0` 时：

len == -1  且 errno == EAGAIN：发送缓冲区满，不是真正的错误；上层 `OnWrite_` 会重新注册 `EPOLLOUT` 事件，等发送缓冲区有空余后继续发送。

len == -1  且 errno == EPIPE：客户端已经关闭连接；上层 `OnWrite_` 会直接调用 `CloseConn_` 关闭连接

`len == 0`：对端关闭连接，同上

这个函数是高性能 HTTP 服务器发送静态文件的标准实现，几乎所有现代 Web 服务器（Nginx、Apache）都采用类似的设计思路。

----

```c++
/**
 * @brief 处理 HTTP 请求
 *
 * 处理流程：
 * 1. 初始化请求解析器
 * 2. 从读缓冲区解析请求
 * 3. 根据解析结果初始化响应
 * 4. 生成响应并写入写缓冲区
 * 5. 设置 iovec 用于发送
 *
 * @return bool 请求是否处理完成
 *         - true: 响应已准备好，可以发送
 *         - false: 请求未完成，需要继续读取
 */
bool HttpConn::process()
{
    //初始化请求解析器
    request_.Init();

    if (readBuff_.ReadableBytes() <= 0)
    {
        //可读缓冲区为空，无数据可读
        return false;
    }
    else if (request_.parse(readBuff_))
    {
        //请求解析成功
        response_.Init(srcDir, request_.path(), request_.IsKeepAlive(), 200);
    }
    else
    {
        //请求解析失败（格式错误）
        response_.Init(srcDir, request_.path(), false, 400);
    }

    //生成响应并写入writeBuff_
    response_.MakeResponse(writeBuff_);

    //设置iov_[0]指向响应头  可读缓冲区起始位置
    iov_[0].iov_base = const_cast<char *>(writeBuff_.Peek());
    iov_[0].iov_len = writeBuff_.ReadableBytes();
    iovCnt_ = 1;

    //如果有文件内容，设置iov_[1]指向文件
    if(response_.FileLen()>0&&response_.File()){
        iov_[1].iov_base = response_.File();
        iov_[1].iov_len = response_.FileLen();
        iovCnt_ = 2;
    }

    LOG_DEBUG("filesize:%d, %d  to %d", response_.FileLen(), iovCnt_, ToWriteBytes());
    return true;
}
```

process函数上接读数据，下接发响应，是整个 Reactor 模型里「业务处理层」的核心入口。

开头要调用 `request_.Init()`是为了**适配 HTTP 长连接（Keep-Alive）**

同一个 TCP 连接（同一个`HttpConn`对象）会处理多个 HTTP 请求，解析器内部会保存「当前解析到哪一步」的状态（比如正在解析请求行 / 请求头 / 请求体）。

每次处理新请求前必须重置状态，否则上一次请求的残留解析状态会导致本次解析完全错乱。

#### 返回 `false` 的两种场景

这个函数返回 `false` 只有一个含义：**请求还没处理完，需要继续等客户端发数据**。

- 场景 1：读缓冲区空（`ReadableBytes() <= 0`），TCP 数据还没到达，继续等
- 场景 2：`request_.parse()` 返回 `false`，请求不完整（比如 TCP 拆包，只到了一半），需要等剩余数据

上层 `OnProcess` 收到 `false` 后，会继续注册 `EPOLLIN` 事件，等待客户端发送剩余数据。

#### 响应初始化的四个参数

```c++
response_.Init(根目录, 请求路径, 是否长连接, 状态码)
```

- `srcDir`：服务器静态资源的根目录（比如 `/var/www/html`）
- `request_.path()`：解析出的请求路径（比如 `/index.html`）
- `request_.IsKeepAlive()`：请求头里有没有 `Connection: keep-alive`，决定响应是否保持长连接
- 状态码：200 成功，400 请求格式错误

**为什么不把文件内容也写进缓冲区？**

这是核心性能优化：

- 文件内容通过 `mmap` 直接映射到内存，不需要再拷贝到写缓冲区
- 后续用 `writev` 聚集写，一次同时发「缓冲区里的响应头」+「mmap 里的文件内容」
- 实现了**零拷贝发送**，比把文件读进缓冲区再发，性能高几倍

#### 配置 `iovec` 的逻辑（和 `writev` 完全对应）

| iov 数组  |      指向哪里       |             内容              |
| :-------: | :-----------------: | :---------------------------: |
| `iov_[0]` | `writeBuff_.Peek()` |          HTTP 响应头          |
| `iov_[1]` | `response_.File()`  | mmap 映射的文件内容（响应体） |

- 如果请求的是纯文本响应（没有文件），就只有 `iov_[0]`，`iovCnt_ = 1`
- 如果请求的是静态文件，就有两段，`iovCnt_ = 2`

**const_cast 的作用：**

`writeBuff_.Peek()` 返回的是 `const char*`（只读指针），而 `iovec.iov_base` 是 `void*` 类型，需要非 const 指针。这里只是发送数据不会修改内容，所以用 `const_cast` 做类型转换是安全的。

```bash
客户端发送HTTP请求
    ↓
epoll触发EPOLLIN
    ↓
OnRead_ → 调用readBuff_.ReadFd() 把数据读进读缓冲区
    ↓
   调用 process() 【就是这个函数】
    ↓
1. 解析请求行、请求头
2. 找到对应文件，mmap映射
3. 生成响应头，写入写缓冲区
4. 配置好iov数组，准备好发送结构
    ↓
process() 返回 true
    ↓
OnProcess 收到true → 注册EPOLLOUT事件
    ↓
epoll触发EPOLLOUT
    ↓
OnWrite_ → 调用 HttpConn::write()
    ↓
用 writev 一次性发送 iov[0]响应头 + iov[1]文件内容
```

