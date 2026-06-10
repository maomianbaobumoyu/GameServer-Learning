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

用vector容器存储char字符，这里读写指针通过原子变量增减保证线程安全，被当作索引，指向vector中正确的位置。

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
```

这里提供了两个不同版本的起始指针，返回带const和不带const版本。BeginPtr_也重载了带const和不带const版本。

在这里BeginWriteConst只能调用const 类型的BeginPtr_。

这是 C++ 最核心的语法规则之一，**和函数体里做了什么无关，只和函数的声明有关**：

> **const 成员函数内部，只能调用其他 const 成员函数**
>
> 绝对不能调用非 const 成员函数！

##### **底层原理：this 指针的类型差异**

所有成员函数内部都有一个隐藏的 `this` 指针，它的类型由函数是否带 `const` 决定：

|          函数类型          |  this 指针类型  |          能调用什么函数          |
| :------------------------: | :-------------: | :------------------------------: |
|  普通成员函数（无 const）  |    `Buffer*`    | 所有成员函数（const + 非 const） |
| const 成员函数（带 const） | `const Buffer*` |   **只能调用 const 成员函数**    |

而BeginWrite调用的是 **普通版 BeginPtr_**，他的this指针是 Buffer*类型。

##### 两个BeginPtr_(const和非const)函数

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