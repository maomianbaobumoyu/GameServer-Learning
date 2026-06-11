```c++
 * Buffer 是一个自动增长的缓冲区，用于：
 * - 存储从套接字读取的数据
 * - 存储待发送的响应数据
 *
 * 设计特点：
 * - 使用 vector<char> 作为底层存储
 * - 读写指针分离，支持高效读写
 * - 自动扩容，无需手动管理内存
 * - 使用原子操作保证线程安全（读写指针）
 *
 * 缓冲区结构：
 * +------------------+------------------+------------------+
 * |  已读取区域      |  可读区域        |  可写区域        |
 * |  (Prependable)   |  (Readable)      |  (Writable)      |
 * +------------------+------------------+------------------+
 * ^                  ^                  ^
 * |                  |                  |
 * buffer_          readPos_          writePos_
```

先看私有成员变量

```c++
    std::vector<char> buffer_;//底层存储
    std::atomic<std::size_t> readPos_;//读指针 对vector的读
    std::atomic<std::size_t> writePos_;//写指针 对vector的写 
```

用vector容器存储char字符，自动扩容与内存复用，无需手动管理。这里读写指针使用原子操作保证**读写指针本身**的线程安全，被当作索引，指向vector中正确的位置。

---

Buffer构造函数

```c++
Buffer::Buffer(int initBuffSize) : buffer_(initBuffSize), readPos_(0), writePos_(0) {}
```

将buffer大小初始化为initBufferSize大小，将读写指针指向头部。

---

### 容量查询函数

```c++
/**
 * @brief 获取可写区域大小
 *
 * 可写区域 = 缓冲区总大小 - 写指针位置
 *
 * @return size_t 可写字节数
 */
size_t Buffer::WritableBytes() const
{
    return buffer_.size() - writePos_;
}

/**
 * @brief 获取可读区域大小
 *
 * 可读区域 = 写指针位置 - 读指针位置
 *
 * @return size_t 可读字节数
 */
size_t Buffer::ReadableBytes() const
{
    return writePos_ - readPos_;
}

/**
 * @brief 获取已读取区域大小（前置区域）
 *
 * 前置区域 = 读指针位置
 * 可用于添加前缀数据
 *
 * @return size_t 前置字节数
 */
size_t Buffer::PrependableBytes() const
{
    return readPos_;
}
```



---

### 读写指针操作函数

```c++
/**
 * @brief 获取可读区域起始指针
 *
 * @return const char* 可读区域指针
 */
const char *Buffer::Peek() const
{
    return BeginPtr_() + readPos_;
}
```

通过 BeginPtr_ () 函数返回的 buffer_ 的C风格指针加上读指针的值返回可读区域起始指针。

```c++
/**
 * @brief 消费（读取）数据
 *
 * 移动读指针，标记数据已消费
 *
 * @param len 消费的字节数
 */
void Buffer::Retrieve(size_t len)
{
    assert(len <= ReadableBytes());
    readPos_ += len;
}
```

先进行断言，判断要移动的len字节是否在可读指针移动的范围内(写指针-读指针),然后移动读指针，表示前面的数据已经消费。

```c++
/**
 * @brief 消费数据直到指定位置
 *
 * @param end 结束位置指针
 */
void Buffer::RetrieveUntil(const char *end)
{
    assert(Peek() <= end);
    Retrieve(end - Peek());
}
```

通过传入C风格指针end,先进行断言，判断end指针是否在可读范围内(比目前读指针位置靠后)，通过Retrieve函数传入相差的len（长度），移动到指定位置。

```c++
/**
 * @brief 清空缓冲区
 *
 * 重置读写指针，清空数据
 */
void Buffer::RetrieveAll()
{
    //清空vector
    bzero(&buffer_[0], buffer_.size());
    readPos_ = 0;
    writePos_ = 0;
}

/**
 * @brief 消费所有数据并返回字符串
 *
 * @return std::string 可读区域的数据
 */
std::string Buffer::RetrieveAllToStr()
{
    //生成一个独立的、深拷贝的 std::string 对象
    std::string str(Peek(), ReadableBytes());
    RetrieveAll();
    return str;
}
```

RetrieveAll清空缓冲区函数，通过bzero(也可以用memset)清空vector(传入第一个元素地址，数组大小)。将读写指针置为0。

RetrieveAllToStr()函数拷贝可读缓冲区内的字符数据生成一个string对象并返回，同时清空缓冲区。

```c++
/**
 * @brief 获取可写区域起始指针（const 版本）
 *
 * @return const char* 可写区域指针
 */
const char* Buffer::BeginWriteConst() const
{
    // 这个函数是 const 的 → this 指针是 const Buffer*
    // 所以 this->BeginPtr_() 只能调用 const 版本的 BeginPtr_()
    // 如果没有 const 版本，编译器直接报错！
    return BeginPtr_() + writePos_;
}

/**
 * @brief 获取可写区域起始指针
 *
 * @return char* 可写区域指针
 */
char *Buffer::BeginWrite()
{
    return BeginPtr_() + writePos_;
}

/**
 * @brief 标记已写入数据
 *
 * 在直接写入数据后调用，更新写指针
 *
 * @param len 已写入的字节数
 */
void Buffer::HasWritten(size_t len)
{
    writePos_ += len;
}
```

通过buffer_容器的起始地址指针加上写指针的值，得到可写区域起始指针。

在写入数据后直接调用HasWritten及时更新写指针位置。

这里提供了两个不同版本的起始指针，返回带const和不带const版本。BeginPtr_也重载了带const和不带const版本。

在这里BeginWriteConst只能调用const 类型的BeginPtr_。

这是 C++ 最核心的语法规则之一，**和函数体里做了什么无关，只和函数的声明有关**：

> **const 成员函数内部，只能调用其他 const 成员函数**
>
> 绝对不能调用非 const 成员函数！

#### **底层原理：this 指针的类型差异**

所有成员函数内部都有一个隐藏的 `this` 指针，它的类型由函数是否带 `const` 决定：

|          函数类型          |  this 指针类型  |          能调用什么函数          |
| :------------------------: | :-------------: | :------------------------------: |
|  普通成员函数（无 const）  |    `Buffer*`    | 所有成员函数（const + 非 const） |
| const 成员函数（带 const） | `const Buffer*` |   **只能调用 const 成员函数**    |

而BeginWrite调用的是 **普通版 BeginPtr_**，他的this指针是 Buffer*类型。

#### 两个BeginPtr_(const和非const)函数

```c++
/**
 * @brief 获取缓冲区起始指针
 *
 * @return char* 缓冲区指针
 */
char *Buffer::BeginPtr_()
{
    return &*buffer_.begin();
    //等价与buffer_.data();
}

/**
 * @brief 获取缓冲区起始指针（const 版本）
 *
 * @return const char* 缓冲区指针
 */
const char *Buffer::BeginPtr_() const
{
    return &*buffer_.begin();
}
```

我们顺便来详细看下这两个辅助函数，以刚才BeginWriteConst的例子可以知道这两种返回值的函数必须都要有，否则const 成员函数没法获取buffer起始指针。

1. **普通版本 `BeginPtr_()`**：给普通成员函数调用（比如 `BeginWrite()`），返回可写的 `char*`，允许修改缓冲区数据
2. **const 版本 `BeginPtr_()`**：给 const 成员函数调用（比如 `BeginWriteConst()`、`Peek()`），返回只读的 `const char*`，禁止修改缓冲区数据

需要看函数的**声明**有没有带 `const`。

- 函数声明带 `const` = 编译器相信 "这个函数绝对不会修改对象"
- 函数声明不带 `const` = 编译器认为 "这个函数可能会修改对象"

**语法强制要求**：

1. const 成员函数的 `this` 指针是 `const T*`，只能调用 const 成员函数
2. 编译器只看函数声明是否带 const，不看函数体里做了什么
3. 所以只要有 const 成员函数需要调用 `BeginPtr_()`，就必须提供 const 版本的 `BeginPtr_()`

再来看BeginPtr_返回时的写法：

`buffer_.begin()`

- 返回值：**`std::vector::iterator`（迭代器）**
- 迭代器 ≠ 原生指针！
- 它是一个**类指针对象**，行为像指针，但**不能直接传给 C 语言接口、系统调用**。

`*buffer_.begin()`

- **解引用迭代器**
- 得到：**vector 第一个元素的引用（char&）**

`&*buffer_.begin()`

- **对元素引用取地址**
- 得到：**指向 vector 底层数组首地址的原生指针 `char\*`**

最后获取到 vector 底层连续内存空间的 **C 语言起始原生指针**。

当然在C++11及之后，可以使用 buffer_ .data() 的方式直接返回底层数组原生指针，这里用  `&*buffer_.begin()` 只是**老式写法**。

---

### 数据追加函数

```c++
/**
 * @brief 追加字符串数据
 *
 * @param str 要追加的字符串
 */
void Buffer::Append(const std::string &str)
{
    //调用另一个Append,传入str(c风格)和长度
    Append(str.data(), str.length());
}

/**
 * @brief 追加任意数据
 *
 * @param data 要追加的数据指针
 * @param len 数据长度
 */
void Buffer::Append(const void *data, size_t len)
{
    assert(data);
    //显式转换成const char*
    Append(static_cast<const char *>(data), len);
}

/**
 * @brief 追加 C 字符串数据
 *
 * 确保可写区域足够大，然后复制数据
 *
 * @param str 要追加的 C 字符串
 * @param len 字符串长度
 */
void Buffer::Append(const char *str, size_t len)
{
    assert(str);
    //确保能写的下，不够就扩容
    EnsureWriteable(len);
    std::copy(str, str + len, BeginWrite());
    HasWritten(len); //更新写入字节
}

/**
 * @brief 追加另一个缓冲区的数据
 *
 * @param buff 要追加的缓冲区
 */
void Buffer::Append(const Buffer &buff)
{
    Append(buff.Peek(), buff.ReadableBytes());
}


/**
 * @brief 确保可写区域足够大
 *
 * 如果空间不足，自动扩容
 *
 * @param len 需要的字节数
 */
void Buffer::EnsureWriteable(size_t len) {
    if(WritableBytes() < len) {
        MakeSpace_(len);
    }
    assert(WritableBytes() >= len);
}
```

这是一个**典型的「分层重载 + 统一底层」设计：4 个`Append`函数形成清晰的3 层调用链**，所有上层函数只做**参数类型转换**，最终所有写入逻辑都收敛到**同一个底层实现**。

整体调用链全景图

```bash
┌─────────────────────────────────────────────────────────┐
│  上层用户接口（4个重载，用户直接调用）                   │
├─────────────┬─────────────┬─────────────┬─────────────┤
│ Append(string) │ Append(void*) │ Append(Buffer) │ Append(char*) │
└───────┬───────┴───────┬───────┴───────┬───────┘
        │               │               │
        └───────────────┼───────────────┘
                        │
┌───────────────────────▼───────────────────────────────┐
│  唯一底层实现（所有写入逻辑的终点）                     │
│          Append(const char* str, size_t len)          │
└───────────────────────┬───────────────────────────────┘
                        │
┌───────────────────────▼───────────────────────────────┐
│  辅助函数（自动处理扩容）                               │
│          EnsureWriteable(len) → MakeSpace_(len)       │
└───────────────────────────────────────────────────────┘
```

**核心规则**：

- 所有上层`Append`都**不包含任何实际写入逻辑**
- 所有上层`Append`最终都会调用**同一个底层`Append(const char*, size_t)`**
- 底层`Append`负责**空间检查、数据复制、指针更新**的完整流程

#####  `Append(const std::string& str)`

**适用场景**：最常用，追加 C++ 标准字符串

**做了什么**：

1. 调用`str.data()`获取`std::string`内部字符数组的**const char \* 指针**
2. 调用`str.length()`获取字符串的**字节长度**
3. 把这两个参数传给底层`Append`

**调用示例**：

```c++
Buffer buf;
buf.Append("HTTP/1.1 200 OK\r\n"); // 自动匹配这个重载
buf.Append(std::string("Content-Length: 123\r\n"));
```

**调用流程**：

```bash
用户调用 Append(string)
    ↓
str.data() → const char*
str.length() → size_t
    ↓
调用 Append(const char*, size_t) 【底层实现】
```

------

#####  `Append(const void* data, size_t len)`

**适用场景**：追加任意二进制数据（结构体、数组、文件内容等）

**做了什么**：

1. 用`static_cast`把无类型的`void*`指针转换成**字节级别的 char \* 指针**
2. 保留用户传入的长度`len`
3. 传给底层`Append`

**为什么需要这个重载？**

`void*`是 C++ 中唯一能接受**任意类型指针**的类型，有了这个重载，可以把任何数据直接写入缓冲区：

```c++
// 写入结构体
struct UserInfo { int id; char name[20]; } user;
buf.Append(&user, sizeof(user));

// 写入int数组
int arr[] = {1,2,3,4};
buf.Append(arr, sizeof(arr));
```

**调用流程**：

```c++
用户调用 Append(void*, len)
    ↓
static_cast<const char*>(data) → const char*
保留 len 参数
    ↓
调用 Append(const char*, size_t) 【底层实现】
```

------

##### `Append(const Buffer& buff)`

**适用场景**：把另一个 Buffer 的可读数据追加到当前 Buffer

**做了什么**：

1. 调用`buff.Peek()`获取源 Buffer**可读区域的起始指针**
2. 调用`buff.ReadableBytes()`获取源 Buffer**可读区域的字节数**
3. 传给底层`Append`

**为什么需要这个重载？**

网络编程中经常需要拼接多个缓冲区的数据，这个重载让操作变得极其简单：

```bash
Buffer readBuf, writeBuf;
// 从socket读到readBuf
readBuf.ReadFd(fd, &errno);
// 把readBuf的数据全部追加到writeBuf
writeBuf.Append(readBuf);
```

**调用流程**：

```bash
用户调用 Append(Buffer&)
    ↓
buff.Peek() → const char*
buff.ReadableBytes() → size_t
    ↓
调用 Append(const char*, size_t) 【底层实现】
```

------

#####  `Append(const char* str, size_t len)`

**这是唯一的底层实现，所有写入逻辑都在这里**

**适用场景**：追加 C 风格字符串（指定长度）

**做了什么**：

1. 断言检查指针不为空
2. 调用`EnsureWriteable(len)`确保缓冲区有足够空间（不够自动扩容）
3. 用`std::copy`把数据从源地址复制到缓冲区的可写区域
4. 调用`HasWritten(len)`更新写指针，标记数据已写入

**这是整个 Buffer 类最核心的写入函数**，所有其他 Append 最终都会走到这里。

**调用流程**：

```c++
调用 Append(const char*, len)
    ↓
assert(str != nullptr)
    ↓
EnsureWriteable(len) → 检查空间，不够则扩容
    ↓
std::copy(str, str+len, BeginWrite()) → 复制数据
    ↓
HasWritten(len) → writePos_ += len
```

------

##### 辅助函数 `EnsureWriteable(size_t len)`

**作用**：在写入数据前，检查缓冲区的可写空间是否足够，如果不够则自动扩容。

**调用时机**：只被底层`Append(const char*, size_t)`调用，所有写入操作都会自动触发空间检查。

**内部逻辑**：

```c++
void Buffer::EnsureWriteable(size_t len) {
    if(WritableBytes() < len) {
        MakeSpace_(len); // 空间不足，调用扩容函数
    }
    assert(WritableBytes() >= len); // 断言确保扩容成功
}
```

##### 核心优势

**1. 100% 代码复用，零重复**

所有的写入逻辑（空间检查、数据复制、指针更新、扩容）都集中在**一个函数**里：

- 修改逻辑只需要改一次
- 所有上层接口自动生效
- 彻底避免了 "改一个地方漏改另一个地方" 的 bug

**2. 接口极其友好**

用户不需要关心缓冲区的内部实现，只需要传入自己拥有的数据类型即可：

- 有`std::string`就传`std::string`
- 有结构体就传结构体指针 + 大小
- 有另一个 Buffer 就传 Buffer
- 不需要做任何手动的类型转换和长度计算

**3. 类型安全**

编译器会自动匹配正确的重载，避免常见错误：

- 传入`std::string`不会被当成 C 风格字符串处理（不会因为中间有 '\0' 而截断）
- 传入`void*`必须指定长度，避免缓冲区溢出

**4. 易于扩展**

如果以后需要支持更多的数据类型，只需要添加一个新的上层重载，不需要修改任何底层逻辑：

```c++
// 新增：支持std::string_view（C++17）
void Append(std::string_view sv) {
    Append(sv.data(), sv.size());
}
```

------

### 扩容函数

```c++
/**
 * @brief 扩容缓冲区
 *
 * 扩容策略：
 * 1. 如果前置区域 + 可写区域 >= 需要的空间：
 *    - 移动数据到缓冲区开头
    - 重置读写指针
 * 2. 否则：
 *    - 重新分配更大的缓冲区
 *
 * @param len 需要的字节数
 */
void Buffer::MakeSpace_(size_t len)
{
    // 1. 判断：总空闲空间(头部+尾部) 是否足够写入 len 字节？
    if (WritableBytes() + PrependableBytes() < len)
    {
        // 情况A：总空间都不够 → **必须重新扩容vector**
        buffer_.resize(writePos_ + len + 1);
        //+1预留一个字节的安全余量，防止边界问题
    }
    else
    {
        // 情况B：总空间够 → **内存紧缩**，把有效数据挪到开头，合并空闲空间
        size_t readable = ReadableBytes();
        // 把【中间的有效数据】复制到【缓冲区开头】
        std::copy(BeginPtr_() + readPos_, BeginPtr_() + writePos_, BeginPtr_());
        // 重置读写指针
        readPos_ = 0;
         // 写指针移到有效数据末尾
        writePos_ = readable;
    }
}
```

这个函数是**缓冲区的「智能空间管理器」**，也是整个 Buffer 类能**自动扩容、高效复用内存**的核心

当缓冲区**尾部可写空间不够**写入新数据时，用**最高效的方式**腾出足够的连续空间：

1. **优先复用内存**：如果头部有已读完的空闲空间，就把有效数据挪到缓冲区开头，合并空闲空间（不分配新内存，最快）
2. **被迫扩容**：如果总空闲空间都不够，才重新分配更大的内存（万不得已才用）

先回顾 3 个核心空间计算

```c++
// 尾部剩余的**连续可写空间**
size_t WritableBytes() const { return buffer_.size() - writePos_; }
// 头部**已读完、废弃的空闲空间**（readPos前面的空间）
size_t PrependableBytes() const { return readPos_; }
// 有效数据（未读、待发送）的大小
size_t ReadableBytes() const { return writePos_ - readPos_; }
```

缓冲区结构可视化：

```bash
+------------------+------------------+------------------+
|  头部空闲区      |  有效数据区      |  尾部可写区      |
| (Prependable)    |  (Readable)      |  (Writable)      |
+------------------+------------------+------------------+
^                  ^                  ^
|                  |                  |
0              readPos_          writePos_        缓冲区末尾
```

**总空闲空间判断**

```c++
if(WritableBytes() + PrependableBytes() < len)
```

- `WritableBytes()`：尾部剩余空间
- `PrependableBytes()`：头部废弃空间
- **两者相加 = 缓冲区的总空闲空间**
- 判断：**总空闲空间都不够写 len 字节** → 只能扩容

------

**情况 A：总空间不足 → 真正扩容**

```c++
buffer_.resize(writePos_ + len + 1);
```

- `writePos_`：当前已写入数据的末尾
- `len`：需要写入的新数据长度
- `resize`：重新分配更大的内存，把旧数据完整拷贝过去
- `+1`：预留 1 个字节的安全余量，防止边界问题

 效果：缓冲区直接变大，尾部腾出足够的连续可写空间

------

**情况 B：总空间足够 → 内存紧缩（核心优化！）**

**不分配新内存，复用头部空闲空间**

```c++
// 保存有效数据的长度
size_t readable = ReadableBytes();

// 把【中间的有效数据】复制到【缓冲区开头】
std::copy(
    BeginPtr_() + readPos_,   // 源：有效数据起始地址
    BeginPtr_() + writePos_,  // 源：有效数据结束地址
    BeginPtr_()               // 目标：缓冲区最开头
);

// 重置读写指针
readPos_ = 0;                 // 读指针移到开头
writePos_ = readable;         // 写指针移到有效数据末尾
```

#### **执行前后对比（可视化）**

**执行前**（空间分散，尾部不够写）：

```bash
+--------+----------------+--------+
| 头部空闲 |  有效数据(400)  | 尾部空闲 |
|  100B   |                |  100B   |
+--------+----------------+--------+
0      100              500      600
readPos=100          writePos=500
总空闲=200B，尾部仅100B不够写
```

**执行后**（数据前移，空闲空间合并）：

```bash
+----------------+------------------+
|  有效数据(400)  |    大段连续空闲区    |
|                |      200B          |
+----------------+------------------+
0              400                600
readPos=0      writePos=400
```

尾部直接腾出**200B 连续空间**，无需扩容

------

#### 这个函数为什么这么设计？

**1. 避免频繁内存分配（性能极高）**

内存分配（`resize`）是**重量级操作**，耗时很长。

这个函数**优先复用内存**，90% 的场景都不需要真正扩容，大幅提升性能。

**2. 保证空间连续**

所有写入操作都需要**连续的可写空间**，移动数据后，空闲空间合并为尾部的一大块连续空间，完美适配`std::copy`、`write`等需要连续内存的函数。

**3. 不丢失有效数据**

只移动**有效数据区**，头部废弃空间直接覆盖，数据安全无丢失。

------

##### 完整调用链路（什么时候触发？）

这个函数是**私有内部函数**，不会被外部调用，调用链：

```
用户调用 Append(数据)
    ↓
EnsureWriteable(len)  // 检查空间够不够
    ↓
空间不足 → 调用 MakeSpace_(len)
```

这是高性能网络缓冲区的**标准设计**，兼顾了内存效率和执行速度

----

### 文件IO函数

```c++
/**
 * @brief 从文件描述符读取数据
 *
 * 使用 readv 分散读：
 * - 优先读取到 Buffer 的 writable 区域
 * - 如果 Buffer 空间不足，额外读取到栈上的临时数组
 *
 * 为什么使用 readv？
 * - 减少系统调用次数
 * - 确保数据完整读取（避免缓冲区空间不足导致数据丢失）
 *
 * @param fd 文件描述符
 * @param saveErrno 保存 errno 值
 * @return ssize_t 读取的字节数，-1 表示错误
 */
ssize_t Buffer::ReadFd(int fd, int *saveErrno)
{
    char buffer[65535];                      //栈上64kb临时数组，用于分散读
    struct iovec iov[2];                     // 分散读的内存块数组，最多2个
    const size_t writable = WritableBytes(); // 获取Buffer当前的可写空间大小

    //分散读，保证数据都读完
    /* 第一个块：Buffer本身的可写区域（优先写这里） */
    iov[0].iov_base = BeginPtr_() + writePos_; // 指向可写区域的起始地址
    iov[0].iov_len = writable;                 //可写区域大小

    /* 第二个块：栈上临时数组（主容器满了就写这里） */

    iov[1].iov_base = buffer;        //指向栈上临时数组
    iov[1].iov_len = sizeof(buffer); //临沭数组大小64kb

    //使用readv一次性读取所有数据
    const ssize_t len = readv(fd, iov, 2); //从fd读数据，分散读到iov的两个块里

    //处理返回结果
    if (len < 0)
    {
        //读出错，保存错误码
        *saveErrno = errno;
    }
    else if (static_cast<size_t>(len) <= writable)
    {
        //当前数据量<=Buffer可写空间，全部读到了Buffer里
        writePos_ += len; //直接更新写指针就行
    }
    else
    {
        //数据量>Buffer可写空间，剩下的数据读到了栈上数组
        writePos_ = buffer_.size();     //先把Buffer的写指针移到末尾(vector的Buffer已经存满了)
        Append(buffer, len - writable); //把栈上数组的剩余数据Append到Buffer(自动扩容)
    }

    return len; //返回总共读取的字节数
}
```

ReadFd函数用 `readv` 分散读技术，解决了 "非阻塞套接字一次性读不完数据" 的经典问题，同时保证了最高的性能。

#### `readv` 系统调用

```c++
#include <sys/uio.h>
ssize_t readv(int fd, const struct iovec *iov, int iovcnt);
```

|   参数   |                             含义                             |
| :------: | :----------------------------------------------------------: |
|   `fd`   |        要读取的文件描述符（这里是客户端 TCP 套接字）         |
|  `iov`   |     指向`iovec`结构体数组的指针，每个元素代表一个内存块      |
| `iovcnt` | `iovec`数组的元素个数，最大不超过`IOV_MAX`（Linux 上是 1024） |

**`iovec` 结构体定义**：

```c++
struct iovec {
    void *iov_base;  // 内存块的起始地址
    size_t iov_len;  // 内存块的大小
};
```

`readv` 是 Linux 内核提供的  分散读（Scatter Read）系统调用，它的本质是：

> **一次系统调用，把内核缓冲区里的连续数据，按顺序分散写到用户态的多个不连续内存块中**

它的执行流程是原子性的：

1. 内核从`fd`对应的内核缓冲区中读取数据
2. 先写满第一个`iovec`，再写第二个，以此类推
3. 要么读完所有能读的数据，要么写满所有`iovec`，要么出错
4. 返回总共读取的字节数

| 返回值 |                        含义                        |
| :----: | :------------------------------------------------: |
| `> 0`  | 成功读取了`n`个字节，已经按顺序写入到`iovec`数组中 |
| `= 0`  |             对端已经关闭了连接（EOF）              |
|  `-1`  |          发生错误，错误码保存在`errno`中           |

#### 为什么不用普通的 `read()`？

非阻塞套接字的 `read()` 只能把数据读到**一个连续的内存块**里。如果缓冲区的可写空间不够，`read()` 会返回 `EAGAIN`，剩下的数据会留在内核缓冲区里，必须等下一次 `EPOLLIN` 事件触发才能继续读。

这会导致两个严重问题：

1. **多次系统调用**：一次完整的 HTTP 请求可能需要多次 `read()` 才能读完，每次都要触发 epoll 事件，性能差
2. **数据延迟**：剩下的数据必须等下一次事件才能处理，增加了请求的响应时间

而 `readv` 分散读完美解决了这个问题：

> `readv` 可以**一次系统调用**，把数据分散读到**多个不连续的内存块**里。不管内核缓冲区里有多少数据，都能一次性全部读出来。

#### 设计思路

> 准备两个 "容器" 来接数据：
>
> 1. **主容器**：Buffer 本身的可写区域（优先用这个）
> 2. **备用容器**：栈上的 64KB 临时数组（主容器满了就用这个）
>
> 用 `readv` 一次性把内核缓冲区里的所有数据读到这两个容器里，然后再把备用容器里的数据合并到主容器中。

这样既保证了**一次性读完所有数据**，又避免了提前扩容 Buffer 带来的内存浪费。

#### 为什么栈上临时数组是 65535 字节（64KB）？

这个大小是经过精心设计的：

- **足够大**：TCP 的最大报文段长度（MSS）通常是 1460 字节，64KB 足够装下 40 多个 TCP 段，几乎能覆盖所有常见的 HTTP 请求大小
- **足够小**：Linux 默认的栈大小是 8MB，64KB 完全不会导致栈溢出
- **性能最高**：栈上内存分配是 O (1) 的，比堆上分配快几个数量级，而且用完自动释放，没有内存泄漏

#### 什么时候会用到栈上临时数组？

只有当**内核缓冲区里的数据量 > Buffer 当前的可写空间**时，才会用到第二个 iovec。

如果数据量小于等于 Buffer 的可写空间，`readv` 只会把数据写到第一个 iovec 里，第二个 iovec 完全不会被用到，栈上数组只是一个备用。

#### 为什么最后要调用 `Append(buff, len - writable)`？

当 Buffer 满了之后，剩下的数据会被读到栈上临时数组里。`Append` 会自动做两件事：

1. 自动扩容 Buffer，确保有足够的空间容纳剩余数据
2. 把栈上数组里的剩余数据复制到扩容后的 Buffer 里

这样最终所有数据都会被合并到 Buffer 中，用户完全感知不到栈上数组的存在。

------

#### 完整执行流程示例

##### 初始状态

- Buffer 总大小：1024 字节
- 当前可写空间：`writable = 1000` 字节（已经写了 24 字节）
- 内核缓冲区里有：`2000` 字节的 HTTP 请求数据

##### 执行步骤

1. **配置 iovec**：
   - iov [0]：指向 Buffer 的可写区域，长度 1000 字节
   - iov [1]：指向栈上 64KB 数组，长度 65535 字节
2. **调用 readv**：
   - 一次性把 2000 字节数据全部读出来
   - 前 1000 字节写到 Buffer 的可写区域
   - 后 1000 字节写到栈上临时数组
   - 返回 `len = 2000`
3. **处理返回结果**：
   - `len = 2000 > writable = 1000`，进入情况 3
   - `writePos_ = 1024`（Buffer 总大小，已经满了）
   - 调用 Append(buff, 1000)：
     - Append 发现可写空间不足，自动扩容 Buffer 到 2025 字节（1024 + 1000 + 1）
     - 把栈上数组里的 1000 字节复制到扩容后的 Buffer 里
     - 更新 writePos_到 2024
4. **最终结果**：
   - Buffer 里有完整的 2000 字节 HTTP 请求数据
   - 栈上数组自动释放，没有任何内存泄漏
   - 一次系统调用完成所有数据读取

这个函数是整个服务器数据读取的入口，调用链是：

```bash
epoll触发EPOLLIN事件
    ↓
主线程调用DealRead_
    ↓
提交读任务到线程池
    ↓
工作线程执行OnRead_
    ↓
调用client->read(&readErrno)
    ↓
调用readBuff_.ReadFd(fd, &readErrno) 【就是这个函数】
    ↓
数据被完整读到readBuff_里
    ↓
调用OnProcess解析请求
```

------

#### 核心优势

**最少的系统调用次数**

不管内核缓冲区里有多少数据，**只需要一次 readv 系统调用**就能全部读完，不需要多次 epoll 触发和 read 调用，性能提升非常明显。

**最高的内存效率**

- 不需要提前扩容 Buffer，只有当数据真的超过可写空间时才会扩容
- 栈上临时数组只有在需要的时候才会被用到，平时不占用任何有效内存
- 避免了 "为了可能的大数据而提前分配大内存" 的浪费

**完美适配非阻塞 IO**

非阻塞套接字的特点是 "一次能读多少就读多少"，而 readv 能保证 "一次把所有能读的都读出来"，完全发挥了非阻塞 IO 的优势。

**对上层完全透明**

上层调用者（比如 HttpConn 的 read 方法）完全不知道栈上数组的存在，只需要调用 `ReadFd` 就能得到完整的数据，接口非常简洁。

`ReadFd` 函数是**高性能网络编程的经典范例**，它用 `readv` 分散读技术，以极小的代价解决了非阻塞 IO 一次性读不完数据的问题。

- 核心思想：**用一个栈上临时数组作为缓冲区的 "扩展"，一次性读完所有数据**
- 优势：最少的系统调用、最高的内存效率、完美适配非阻塞 IO
- 对上层透明：用户只需要关心最终 Buffer 里的完整数据

这也是为什么几乎所有现代高性能网络库（muduo、libevent、libev）都采用类似的设计。

----

```c++
/**
 * @brief 向文件描述符写入数据
 *
 * @param fd 文件描述符
 * @param saveErrno 保存 errno 值
 * @return ssize_t 写入的字节数，-1 表示错误
 */
ssize_t Buffer::WriteFd(int fd, int* saveErrno) {
    size_t readSize = ReadableBytes();
    ssize_t len = write(fd, Peek(), readSize);
    if(len < 0) {
        *saveErrno = errno;
        return len;
    }
    readPos_ += len;
    return len;
}
```

这是写函数，这里不需要像readv一样分散到多个容器里。通过write直接一块发送过去，如果发送失败(比如对端的接收缓存区满了)，会设置errno，并返回写入的len长度。发送成功则及时更新读指针。

### 完整源码

```c++
#include "buffer.h"

//初始化缓冲区大小
Buffer::Buffer(int initBuffSize) : buffer_(initBuffSize), readPos_(0), writePos_(0) {}

// ==================== 容量查询 ====================

/**
 * @brief 获取可写区域大小
 *
 * 可写区域 = 缓冲区总大小 - 写指针位置
 *
 * @return size_t 可写字节数
 */
size_t Buffer::WritableBytes() const
{
    return buffer_.size() - writePos_;
}

/**
 * @brief 获取可读区域大小
 *
 * 可读区域 = 写指针位置 - 读指针位置
 *
 * @return size_t 可读字节数
 */
size_t Buffer::ReadableBytes() const
{
    return writePos_ - readPos_;
}

/**
 * @brief 获取已读取区域大小（前置区域）
 *
 * 前置区域 = 读指针位置
 * 可用于添加前缀数据
 *
 * @return size_t 前置字节数
 */
size_t Buffer::PrependableBytes() const
{
    return readPos_;
}

// ==================== 读写指针 ====================

/**
 * @brief 获取可读区域起始指针
 *
 * @return const char* 可读区域指针
 */
const char *Buffer::Peek() const
{
    return BeginPtr_() + readPos_;
}

/**
 * @brief 消费（读取）数据
 *
 * 移动读指针，标记数据已消费
 *
 * @param len 消费的字节数
 */
void Buffer::Retrieve(size_t len)
{
    assert(len <= ReadableBytes());
    readPos_ += len;
}

/**
 * @brief 消费数据直到指定位置
 *
 * @param end 结束位置指针
 */
void Buffer::RetrieveUntil(const char *end)
{
    assert(Peek() <= end);
    Retrieve(end - Peek());
}

/**
 * @brief 清空缓冲区
 *
 * 重置读写指针，清空数据
 */
void Buffer::RetrieveAll()
{
    //清空vector
    bzero(&buffer_[0], buffer_.size());
    readPos_ = 0;
    writePos_ = 0;
}

/**
 * @brief 消费所有数据并返回字符串
 *
 * @return std::string 可读区域的数据
 */
std::string Buffer::RetrieveAllToStr()
{
    //生成一个独立的、深拷贝的 std::string 对象
    std::string str(Peek(), ReadableBytes());
    RetrieveAll();
    return str;
}

/**
 * @brief 获取可写区域起始指针（const 版本）
 *
 * @return const char* 可写区域指针
 */
const char *Buffer::BeginWriteConst() const
{
    return BeginPtr_() + writePos_;
}

/**
 * @brief 获取可写区域起始指针
 *
 * @return char* 可写区域指针
 */
char *Buffer::BeginWrite()
{
    return BeginPtr_() + writePos_;
}

/**
 * @brief 标记已写入数据
 *
 * 在直接写入数据后调用，更新写指针
 *
 * @param len 已写入的字节数
 */
void Buffer::HasWritten(size_t len)
{
    writePos_ += len;
}

// ==================== 数据追加 ====================

/**
 * @brief 追加字符串数据
 *
 * @param str 要追加的字符串
 */
void Buffer::Append(const std::string &str)
{
    //调用另一个Append,传入str(c风格)和长度
    Append(str.data(), str.length());
}

/**
 * @brief 追加任意数据
 *
 * @param data 要追加的数据指针
 * @param len 数据长度
 */
void Buffer::Append(const void *data, size_t len)
{
    assert(data);
    //显式转换成const char*
    Append(static_cast<const char *>(data), len);
}

/**
 * @brief 追加 C 字符串数据
 *
 * 确保可写区域足够大，然后复制数据
 *
 * @param str 要追加的 C 字符串
 * @param len 字符串长度
 */
void Buffer::Append(const char *str, size_t len)
{
    assert(str);
    //确保能写的下，不够就扩容
    EnsureWriteable(len);
    std::copy(str, str + len, BeginWrite());
    HasWritten(len); //更新写入字节
}

/**
 * @brief 追加另一个缓冲区的数据
 *
 * @param buff 要追加的缓冲区
 */
void Buffer::Append(const Buffer &buff)
{
    Append(buff.Peek(), buff.ReadableBytes());
}

/**
 * @brief 确保可写区域足够大
 *
 * 如果空间不足，自动扩容
 *
 * @param len 需要的字节数
 */
void Buffer::EnsureWriteable(size_t len)
{
    if (WritableBytes() < len)
    {
        MakeSpace_(len);
    }
    assert(WritableBytes() >= len);
}

// ==================== 文件 IO ====================

/**
 * @brief 从文件描述符读取数据
 *
 * 使用 readv 分散读：
 * - 优先读取到 Buffer 的 writable 区域
 * - 如果 Buffer 空间不足，额外读取到栈上的临时数组
 *
 * 为什么使用 readv？
 * - 减少系统调用次数
 * - 确保数据完整读取（避免缓冲区空间不足导致数据丢失）
 *
 * @param fd 文件描述符
 * @param saveErrno 保存 errno 值
 * @return ssize_t 读取的字节数，-1 表示错误
 */
ssize_t Buffer::ReadFd(int fd, int *saveErrno)
{
    char buffer[65535];                      //栈上64kb临时数组，用于分散读
    struct iovec iov[2];                     // 分散读的内存块数组，最多2个
    const size_t writable = WritableBytes(); // 获取Buffer当前的可写空间大小

    //分散读，保证数据都读完
    /* 第一个块：Buffer本身的可写区域（优先写这里） */
    iov[0].iov_base = BeginPtr_() + writePos_; // 指向可写区域的起始地址
    iov[0].iov_len = writable;                 //可写区域大小

    /* 第二个块：栈上临时数组（主容器满了就写这里） */

    iov[1].iov_base = buffer;        //指向栈上临时数组
    iov[1].iov_len = sizeof(buffer); //临沭数组大小64kb

    //使用readv一次性读取所有数据
    const ssize_t len = readv(fd, iov, 2); //从fd读数据，分散读到iov的两个块里

    //处理返回结果
    if (len < 0)
    {
        //读出错，保存错误码
        *saveErrno = errno;
    }
    else if (static_cast<size_t>(len) <= writable)
    {
        //当前数据量<=Buffer可写空间，全部读到了Buffer里
        writePos_ += len; //直接更新写指针就行
    }
    else
    {
        //数据量>Buffer可写空间，剩下的数据读到了栈上数组
        writePos_ = buffer_.size();     //先把Buffer的写指针移到末尾(vector的Buffer已经存满了)
        Append(buffer, len - writable); //把栈上数组的剩余数据Append到Buffer(自动扩容)
    }

    return len; //返回总共读取的字节数
}

/**
 * @brief 向文件描述符写入数据
 *
 * @param fd 文件描述符
 * @param saveErrno 保存 errno 值
 * @return ssize_t 写入的字节数，-1 表示错误
 */
ssize_t Buffer::WriteFd(int fd, int *saveErrno)
{
    size_t readSize = ReadableBytes(); //获取可读区域大小
    ssize_t len = write(fd, Peek(), readSize);
    if (len < 0)
    {
        *saveErrno = errno; //保存错误码
        return len;
    }
    //更新读指针，标记已发送的数据
    readPos_ += len;
    //返回实际发送的字节数
    return len;
}

// ==================== 辅助函数 ====================

/**
 * @brief 获取缓冲区起始指针
 *
 * @return char* 缓冲区指针
 */
char *Buffer::BeginPtr_()
{
    return &*buffer_.begin();
    //等价与buffer_.data();
}

/**
 * @brief 获取缓冲区起始指针（const 版本）
 *
 * @return const char* 缓冲区指针
 */
const char *Buffer::BeginPtr_() const
{
    return &*buffer_.begin();
}

/**
 * @brief 扩容缓冲区
 *
 * 扩容策略：
 * 1. 如果前置区域 + 可写区域 >= 需要的空间：
 *    - 移动数据到缓冲区开头
    - 重置读写指针
 * 2. 否则：
 *    - 重新分配更大的缓冲区
 *
 * @param len 需要的字节数
 */
void Buffer::MakeSpace_(size_t len)
{
    // 1. 判断：总空闲空间(头部+尾部) 是否足够写入 len 字节？
    if (WritableBytes() + PrependableBytes() < len)
    {
        // 情况A：总空间都不够 → **必须重新扩容vector**
        buffer_.resize(writePos_ + len + 1);
        //+1预留一个字节的安全余量，防止边界问题
    }
    else
    {
        // 情况B：总空间够 → **内存紧缩**，把有效数据挪到开头，合并空闲空间
        size_t readable = ReadableBytes();
        // 把【中间的有效数据】复制到【缓冲区开头】
        std::copy(BeginPtr_() + readPos_, BeginPtr_() + writePos_, BeginPtr_());
        // 重置读写指针
        readPos_ = 0;
         // 写指针移到有效数据末尾
        writePos_ = readable;
    }
}

```

