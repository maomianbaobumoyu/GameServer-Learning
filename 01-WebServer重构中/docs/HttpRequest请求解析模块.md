这是一个**HTTP/1.1 请求解析模块**，负责把客户端发来的原始 HTTP 字节流报文，解析成程序可直接读取的结构化数据；同时内置了登录 / 注册的用户验证逻辑，对接 MySQL 数据库连接池。

整体采用 **有限状态机（FSM） + 正则表达式** 的经典实现方案，逐行解析 HTTP 报文，严格遵循 HTTP 协议的报文结构规范。

### 核心枚举定义

#### 解析状态枚举

```c++
    enum PARSE_STATE
    {
        REQUEST_LINE, /**< 解析请求行 */
        HEADERS,      /**< 解析请求头 */
        BODY,         /**< 解析请求体 */
        FINISH,       /**< 解析完成 */
    };

```

这是状态机的核心，定义了解析流程的4个阶段，和HTTP报文的结构一一对应。

HTTP 标准报文结构 = 请求行 + 若干请求头 + 空行 + 请求体，状态机严格按照这个顺序单向流转，不会跳步。

#### 请求结果枚举

```c++
    //HTTP响应码枚举
    enum HTTP_CODE
    {
        NO_REQUEST = 0,     /**< 请求不完整 */
        GET_REQUEST,        /**< 完整 GET 请求 */
        BAD_REQUEST,        /**< 错误请求 */
        NO_RESOURSE,        /**< 资源不存在 */
        FORBIDDENT_REQUEST, /**< 禁止访问 */
        FILE_REQUEST,       /**< 文件请求 */
        INTERNAL_ERROR,     /**< 内部错误 */
        CLOSED_CONNECTION,  /**< 连接已关闭 */
    };
```

定义了解析后的请求类型 / 处理结果，是解析模块和上层响应模块之间的 "交互协议"。

### 私有成员(变量和静态成员)

这里包括私有成员变量和私有静态成员

```c++
    // ==================== 成员变量 ====================

    PARSE_STATE state_;                                   //当前解析状态
    std::string method_;                                  //请求方法(GET/POST)
    std::string path_;                                    //请求路径
    std::string version_;                                 //HTTP版本
    std::string body_;                                    //请求体
    std::unordered_map<std::string, std::string> header_; //请求头映射表
    std::unordered_map<std::string, std::string> post_;   //post参数映射表

    // ==================== 静态成员 ====================

    /**
     * @brief 默认 HTML 页面集合
     *
     * 这些路径会被映射到对应的 .html 文件
     */
    static const std::unordered_set<std::string> DEFAULT_HTML;

    /**
     * @brief 默认 HTML 标签映射表
     *
     * 用于区分注册和登录页面
     * 0: 注册页面, 1: 登录页面
     */
    static const std::unordered_map<std::string, int> DEFAULT_HTML_TAG;

    /**
     * @brief 将十六进制字符转换为整数
     *
     * 用于 URL 解码
     *
     * @param ch 十六进制字符（0-9, A-F, a-f）
     * @return int 对应的整数值
     */
    static int ConverHex(char ch);


    /**
     * @brief 验证用户（注册/登录）
     *
     * 通过数据库验证用户名和密码
     *
     * @param name 用户名
     * @param pwd 密码
     * @param isLogin 是否为登录操作（false 表示注册）
     * @return bool 验证是否成功
     */
    static bool UserVerify(const std::string& name, const std::string& pwd, bool isLogin);
```

这里在ParseRequestLine_ 函数通过regex正则表达式得到键值对，再赋值给method_ 等成员变量，同时赋给state_ = HEADERS，改成解析请求头状态。在ParseHeader_ 解析请求头函数中，通过regex正则表达式得到键值对，存入header_ 哈希表中方便查找。

如果是POST请求，在函数ParseFromUrlencoded_ 中解析表单数据，并存储到post_ 哈希表中。

```c++
// ==================== 静态成员初始化 ====================

/**
 * @brief 默认 HTML 页面集合
 *
 * 这些路径会被映射到对应的 .html 文件
 */
const unordered_set<string> HttpRequest::DEFAULT_HTML{
    "/index", "register", "/login",
    "welcome", "video", "/picture"};

/**
 * @brief 默认 HTML 标签映射表
 *
 * 用于区分注册和登录页面：
 * - "/register.html" -> 0 (注册)
 * - "/login.html" -> 1 (登录)
 */
const unordered_map<string, int> HttpRequest::DEFAULT_HTML_TAG{
    {"/register.html", 0}, {"/login.html", 1}};

// ==================== URL 解码辅助函数 ====================

/**
 * @brief 将十六进制字符转换为整数
 *
 * 用于 URL 解码
 * 示例：'A' -> 10, 'f' -> 15
 *
 * @param ch 十六进制字符
 * @return int 对应的整数值
 */
int HttpRequest::ConverHex(char ch) {
    if (ch >= '0' && ch <= '9') return ch - '0';  //处理数字字符
    if (ch >= 'A' && ch <= 'F') return ch - 'A' + 10;
    if (ch >= 'a' && ch <= 'f') return ch - 'a' + 10;
    return 0; // 非法十六进制字符，默认返回0，也可以根据需求做错误处理
}
```

在.cpp文件中给静态成员初始化，DEFAULT_HTML 哈希表中存储服务器拥有的请求页面。DEFAULT_HTML_TAG 则用于区分注册和登陆页面,方便之后拓展其他页面，在 ParsePost_ 函数中进行判断。

ConverHex函数在ParseFromUrlencoded_ 函数中做16进制转换。

UserVerify函数在后面解析POST请求的时候再讲解。

### 构造与析构函数

```c++
    /**
     * @brief 构造函数
     *
     * 初始化解析器状态
     */
    HttpRequest() { Init(); }

    ~HttpRequest() = default;

    /**
     * @brief 初始化/重置解析器状态
     *
     * 清空所有成员变量，重置状态机
     */
    void Init();

/**
 * @brief 初始化/重置解析器状态
 *
 * 清空所有成员变量，重置状态机到 REQUEST_LINE 状态
 */
void HttpRequest::Init()
{
    method_ = path_ = version_ = body_ = "";
    state_ = REQUEST_LINE;
    header_.clear();
    post_.clear();
}
```

这里构造函数调用Init，析构函数为默认提供的。

在Init函数中给成员变量初始化为空，设置解析状态为解析请求行，并清空header_ 和 post_ 哈希表。

### 主解析函数

 `parse` 函数是 **HTTP 请求解析的总入口与核心驱动**，它采用「有限状态机 + 逐行读取」的设计，从环形缓冲区 `Buffer` 中读取原始字节流，严格按照 HTTP 报文的结构顺序（请求行 → 请求头 → 请求体）分阶段解析，支持 TCP 流式分包场景下的断点续解析。

```c++
/**
 * @brief 解析 HTTP 请求
 *
 * 使用状态机逐行解析 HTTP 请求报文
 *
 * 解析流程：
 * 1. 从缓冲区中查找行结束符 "\r\n"
 * 2. 根据当前状态调用对应的解析函数
 * 3. 状态转换：REQUEST_LINE -> HEADERS -> BODY -> FINISH
 *
 * @param buff 读缓冲区
 * @return bool 解析是否成功
 */
bool HttpRequest::parse(Buffer &buff)
{
    const char CRLF[] = "\r\n"; //HTTP行结束符

    //缓冲区中没有数据
    if (buff.ReadableBytes() <= 0)
    {
        return false;
    }

    //循环解析，直到状态为 FINISH 或缓冲区没有数据
    while (buff.ReadableBytes() && state_ != FINISH)
    {
        //在可读区间内查找行结束符
        const char *lineEnd = search(buff.Peek(), buff.BeginWriteConst(), CRLF, CRLF + 2);
        // 提取一行（不包含 "\r\n"）
        std::string line(buff.Peek(), lineEnd);

        //根据当前状态调用对应的解析函数
        switch (state_)
        {
        case REQUEST_LINE:
            if (!ParseRequestLine_(line))
            {
                return false;
            }
            //做路径映射，比如把/补全为/index.html
            ParsePath_();
            break;
        case HEADERS:
            ParseHeader_(line);
            //如果缓冲区数据<=2（只剩"\r\n"),说明头部解析完成
            if (buff.ReadableBytes() <= 2)
            {                    //只剩刚好2字节，说明这是GET类无请求体请求
                state_ = FINISH; //直接设为FINISH，跳过BODY阶段
            }
            break;
        case BODY:
            ParseBody_(line);
            break;
        default:
            break;
        }

        //如果达到缓冲区末尾，退出循环
        if (lineEnd == buff.BeginWrite())
        {
            break;
        }

        //移动读指针到下一行
        buff.RetrieveUntil(lineEnd + 2);
    }
    LOG_DEBUG("[%s], [%s], [%s]", method_.c_str(), path_.c_str(), version_.c_str());
    return true;
}
```

以 HTTP 标准行结束符 `\r\n` 为单位，从缓冲区中逐行提取数据；

根据当前解析状态 `state_`，把该行数据交给对应阶段的解析函数处理；

解析完一行就移动缓冲区读指针，消费掉已处理数据；

循环执行，直到**报文解析完成（`FINISH`）**或 **缓冲区数据不足（一行不完整）**；

仅当请求行格式非法时返回 `false`，其余情况返回 `true`（数据没收完也返回 true，下次继续解析）。

#### 核心行读取逻辑

- `buff.Peek()`：返回缓冲区可读区域的起始指针；
- `buff.BeginWriteConst()`：返回缓冲区可写区域的起始指针，也就是可读区域的尾后指针；
- `search(...)`：STL 算法，在 `[Peek(), BeginWriteConst())` 左闭右开区间内，查找 `\r\n` 首次出现的位置，返回指向 `\r` 的指针；
- 最终 `line` 就是**去掉末尾 `\r\n` 的一行纯文本**。

#### 循环退出的两种情况

1. **正常解析完成**：`state_` 变为 `FINISH`，整个 HTTP 报文解析完毕；
2. **数据不完整**：没找到 `\r\n`，说明当前收到的数据凑不齐一整行（TCP 流式传输分包导致），直接退出循环，等待下一批数据到达后，从当前状态继续解析。

> 这就是非阻塞解析的核心：不等待完整报文，来多少解析多少，状态自动保留，适配 Reactor 高并发模型。

------

#### 状态机核心分支（switch 部分）

状态单向流转：`REQUEST_LINE` → `HEADERS` → `BODY` → `FINISH`，严格对应 HTTP 报文的结构顺序。

#### ① 状态：REQUEST_LINE（解析请求行）

```c++
case REQUEST_LINE:
    if(!ParseRequestLine_(line)) {
        return false;
    }
    ParsePath_();
    break;
```

- 只在解析第一行时进入，调用 `ParseRequestLine_` 用正则提取「请求方法、请求路径、HTTP 版本」；
- **解析失败直接返回 `false`**：请求行是 HTTP 报文的核心，格式非法说明这不是合法 HTTP 请求，直接终止解析；
- 解析成功后调用 `ParsePath_()` 做路径映射（比如把 `/` 补全为 `/index.html`）；
- 状态流转：`ParseRequestLine_` 内部解析成功后，会自动把 `state_` 改为 `HEADERS`，下一轮循环自动进入请求头解析。

#### ② 状态：HEADERS（解析请求头）

```c++
case HEADERS:
    ParseHeader_(line);
    // 缓冲区只剩 \r\n（空行），说明无请求体，直接结束
    if(buff.ReadableBytes() <= 2) {
        state_ = FINISH;
    }
    break;
```

- 逐行调用 `ParseHeader_`，通过正则把 `Key: Value` 格式的请求头存入 `header_` 哈希表；
- **空行触发状态切换**：当遇到分隔请求头和请求体的空行时，`line` 为空字符串，正则匹配失败，`ParseHeader_` 内部会自动把 `state_` 改为 `BODY`；
- 优化逻辑 `ReadableBytes() <= 2`：
  - 如果缓冲区剩余可读数据刚好 2 字节（就是空行的 `\r\n`），说明这是 GET 类无请求体的请求，直接把状态设为 `FINISH`，跳过 BODY 阶段，提前结束解析；
  - 如果是 POST 请求，空行后还有请求体数据，剩余字节数一定大于 2，不会触发这个判断，会正常进入 BODY 状态。

#### ③ 状态：BODY（解析请求体）

```c++
case BODY:
    ParseBody_(line);
    break;
```

- 把剩余的全部数据当作请求体内容传入 `ParseBody_`；
- `ParseBody_` 内部会保存原始请求体，同时如果是 POST 表单请求，会触发 URL 解码、参数解析、数据库登录 / 注册校验，最后把 `state_` 设为 `FINISH`，解析正式完成。

> 补充实现细节：该项目做了简化处理 —— 默认请求体一次性全部到达缓冲区。如果 body 数据分包到达，首次解析会停在 HEADERS 状态，等 body 数据收全后才会进入 BODY 完成解析。

------

####  收尾与返回值

```c++
LOG_DEBUG("[%s], [%s], [%s]", method_.c_str(), path_.c_str(), version_.c_str());
return true;
```

- 打印解析结果的调试日志；
- 返回 `true` 仅代表本次解析过程没有发生格式错误，不代表整个请求解析完成。
  - 数据没收完时，`state_` 可能还停留在 `REQUEST_LINE` 或 `HEADERS`，下次调用 `parse` 会从当前状态继续；
  - 只有 `state_ == FINISH` 时，才代表一整条 HTTP 请求解析完毕，可以读取结果。

----

#### `std::search` 算法详解

代码里的 `search(...)` 是 C++ 标准库提供的**序列查找算法**，作用是：**在一个大的主序列中，查找一个子序列第一次出现的位置**。

通用形式：

```c++
search(主序列起始, 主序列结束, 子序列起始, 子序列结束);
```

它遵循 C++ 标准库的**左闭右开**约定：

- 主序列的查找范围是 `[主序列起始, 主序列结束)`，包含起始、不包含结束；
- 要查找的子序列范围是 `[子序列起始, 子序列结束)`；
- 返回值：如果找到子序列，返回指向**子序列第一个元素**的迭代器 / 指针；如果没找到，返回主序列的结束迭代器 / 指针。

```c++
const char* lineEnd = search(buff.Peek(), buff.BeginWriteConst(), CRLF, CRLF + 2);
```

|           参数           |                             含义                             |
| :----------------------: | :----------------------------------------------------------: |
|      `buff.Peek()`       | 主序列起点：缓冲区**可读区域的首地址**，也就是当前待解析数据的开头 |
| `buff.BeginWriteConst()` | 主序列终点：缓冲区**可写区域的首地址**，即可读区域的尾后位置 |
|          `CRLF`          |         子序列起点：要查找的行结束符 `\r\n` 的首地址         |
|        `CRLF + 2`        | 子序列终点：`\r\n` 一共 2 个字符（`\r` 和 `\n`），+2 正好指向结束位置 |

**返回值 `lineEnd`**

- 如果在缓冲区里找到了 `\r\n`：`lineEnd` 指向 `\r` 的位置（子序列的第一个字符）；
- 如果没找到 `\r\n`（数据不够一整行）：`lineEnd` 等于 `buff.BeginWriteConst()`，也就是主序列的终点。

**为什么用 `search` 而不用 `strstr`？**

`strstr` 是 C 语言字符串查找函数，**依赖字符串结尾的 `\0`**；

而网络缓冲区是纯字节流，没有 `\0` 结束符，数据长度由可读字节数决定。`search` 可以手动指定查找的起止范围，天然适配这种无结束符的字节流场景，是非阻塞网络编程的常规写法。

------

#### `line` 字符串的构造

```c++
std::string line(buff.Peek(), lineEnd);
```

这是 `std::string` 的**迭代器区间构造函数**（指针也是一种特殊的迭代器），作用是：

取 `[buff.Peek(), lineEnd)` 左闭右开区间内的所有字符，构造出一个新的字符串。

----

因为 `lineEnd` 指向的是 `\r` 的位置，所以这个区间**正好包含一行的纯文本内容，自动剔除了末尾的 `\r\n`**。

举个直观例子：

缓冲区里的原始字节是：`GET /index.html HTTP/1.1\r\nHost: 127.0.0.1\r\n`

- `buff.Peek()` 指向开头的 `G`
- `search` 找到第一个 `\r`，`lineEnd` 指向这个 `\r`
- 构造出的 `line` 就是 `GET /index.html HTTP/1.1`，没有换行符

这样得到的 `line` 可以直接传给后续的解析函数（请求行解析、请求头解析），不需要额外处理换行符。

结合后续代码，这一套「查找 → 提取 → 消费」的完整逻辑是：

1. **找换行**：用 `search` 在当前可读数据里找 `\r\n`；

2. **切行**：用找到的位置构造 `line`，得到一行纯文本；

3. **解析行**：根据当前状态机状态，把 `line` 传给对应的解析函数；

4. 判断是否收齐一行：

   ```c++
   if(lineEnd == buff.BeginWrite()) { break; }
   ```

   如果 `lineEnd`等于缓冲区写指针，说明没找到 `\r\n`，当前数据凑不齐一整行（TCP 分包导致），直接退出循环，等待下一批数据到达后继续解析。

5. 消费已处理数据：

   ```c++
   buff.RetrieveUntil(lineEnd + 2);
   ```

   把缓冲区的读指针向后移动 `lineEnd + 2` 个位置：+2  是为了跳过 `\r和\n`两个字符，让读指针指向下一行的开头，为下一轮循环做准备。

------

#### 不同状态下 `line` 的内容

因为状态机是单向流转的，不同阶段切出来的 `line` 含义完全不同：

- `REQUEST_LINE` 状态：`line` 是完整的请求行，如 `GET / HTTP/1.1`
- `HEADERS` 状态：`line` 是单条请求头，如 `Connection: keep-alive`；如果遇到空行（`line` 为空字符串），说明请求头解析完毕，自动切换到 `BODY` 状态
- `BODY` 状态：`line` 是完整的请求体内容（如 POST 表单数据）

### 路径解析

```c++
/**
 * @brief 解析请求路径
 *
 * 将路径映射到实际文件：
 * - "/" -> "/index.html"
 * - "/register" -> "/register.html"
 * - "/login" -> "/login.html"
 */
void HttpRequest::ParsePath_()
{
    if (path_ == "/")
    {
        path_ = "/index.html";
    }
    else
    {
        for (auto &item : DEFAULT_HTML)
        {
            if (item == path_)
            {
                path_ += ".html";
                break;
            }
        }
    }
}
```

在前面的主解析函数状态机中解析请求行调用，补全文件名。

### 请求行解析

```c++
/**
 * @brief 解析请求行
 *
 * 格式：METHOD PATH HTTP/VERSION
 * 示例：GET /index.html HTTP/1.1
 *
 * 正则表达式：^([^ ]*) ([^ ]*) HTTP/([^ ]*)$
 *   - ([^ ]*) 匹配 METHOD
 *   - ([^ ]*) 匹配 PATH
 *   - ([^ ]*) 匹配 VERSION（去掉 "HTTP/" 前缀）
 *
 * @param line 请求行字符串
 * @return bool 解析是否成功
 */
bool HttpRequest::ParseRequestLine_(const string &line)
{
    regex patten("^([^ ]*) ([^ ]*) HTTP/([^ ]*)$");
    smatch subMatch;
    if (regex_match(line, subMatch, patten))
    {
        method_ = subMatch[1];  //请求方法
        path_ = subMatch[2];    //请求路径
        version_ = subMatch[3]; //HTTP版本
        state_ = HEADERS;       //状态转移到HEADERS
        return true;
    }
    LOG_ERROR("RequestLine Error");
    return false;
}
```

`ParseRequestLine_` 是 HTTP 状态机解析的**第一阶段核心函数**，专门负责处理 HTTP 请求报文的第一行（请求行）

用正则严格校验该行是否符合HTTP请求行格式，并拆分字段更改state_ 准备解析请求头。

通过regex存入正则表达式，通过regex_match做整串完全匹配，放入subMatch中。

HTTP 请求行的标准格式是固定的：

```bash
请求方法 空格 请求路径 空格 HTTP/版本号
```

比如：`GET /index.html HTTP/1.1`

正则就是严格按照这个格式编写，用 `()` 划分出三个「捕获组」，分别提取我们需要的三个字段。

|  正则片段   |                           含义说明                           |
| :---------: | :----------------------------------------------------------: |
|     `^`     | 匹配字符串的**起始位置**，强制从行首开始匹配，避免行首出现多余字符 |
|  `([^ ]*)`  | 第 1 个捕获组，提取请求方法`[^ ]`：字符集，匹配「不是空格」的任意字符`*`：匹配 0 次或多次整体效果：匹配第一个空格之前的所有字符，即 `GET` / `POST` 等方法 |
| ` `（空格） |       严格匹配一个空格，对应请求方法和路径之间的分隔符       |
|  `([^ ]*)`  | 第 2 个捕获组，提取请求路径规则同上，匹配第二个空格之前的所有字符，即 `/index.html` 等路径 |
|  ` HTTP/`   | 严格匹配固定字符串 ` HTTP/`（前面带一个空格），对应 HTTP 协议的固定前缀 |
|  `([^ ]*)`  | 第 3 个捕获组，提取 HTTP 版本号匹配行尾之前的所有非空格字符，即 `1.1` / `1.0`，自动去掉了 `HTTP/` 前缀 |
|     `$`     | 匹配字符串的**结束位置**，强制匹配到行尾，保证行尾没有多余字符 |

#### 为什么用 `regex_match` 而不是 `regex_search`？

- `regex_match`：要求**整个字符串完全匹配**正则，差一个字符都算失败；
- `regex_search`：只要字符串里有任意一段匹配就算成功。

请求行是严格的结构化格式，必须整行都符合标准才是合法请求，因此用 `regex_match` 做严格校验，避免非法报文混入。

#### 匹配结果容器 `smatch`

```c++
smatch subMatch;
```

`std::smatch` 是正则匹配的结果容器，用来保存所有匹配到的内容和捕获组。

它的下标规则非常重要：

- `subMatch[0]`：**整个正则匹配到的完整字符串**（也就是整行请求行）；
- `subMatch[1]`：第 1 个捕获组的内容（请求方法）；
- `subMatch[2]`：第 2 个捕获组的内容（请求路径）；
- `subMatch[3]`：第 3 个捕获组的内容（HTTP 版本号）。

### 请求头解析

```c++
/**
 * @brief 解析请求头
 *
 * 格式：Key: Value
 * 示例：Host: localhost:1316
 *
 * 正则表达式：^([^:]*): ?(.*)$
 *   - ([^:]*) 匹配 Key
 *   - (.*) 匹配 Value
 *
 * 如果正则匹配失败，说明头部解析完成，状态转换到 BODY
 *
 * @param line 请求头字符串
 */
void HttpRequest::ParseHeader_(const string &line)
{
    regex patten("^([^:]*): ?(.*)$");
    smatch subMatch;
    if (regex_match(line, subMatch, patten))
    {
        header_[subMatch[1]] = subMatch[2];
    }
    else
    {
        state_ = BODY; //头部解析完成
    }
}
```

这个 `ParseHeader_` 是 HTTP 解析状态机中 **HEADERS（请求头解析）阶段** 的处理函数。

职责有两个：

1. 正常请求头行：按 `Key: Value` 格式拆分出键和值，存入 `header_` 哈希表，供后续业务逻辑读取；
2. 遇到分隔空行：正则匹配失败时，自动识别出请求头和请求体之间的空行，将状态机切换到 `BODY` 阶段，准备解析请求体。

面按正确的正则 `^([^:]*): ?(.*)$` 逐段拆解：

| 正则片段  |                           作用说明                           |
| :-------: | :----------------------------------------------------------: |
|    `^`    | 行首锚点，强制从字符串开头开始匹配，避免行首出现多余非法字符 |
| `([^:]*)` | 第 1 个捕获组，提取请求头的 **Key（键名）**`[^:]` 是否定字符集：匹配任意**不是冒号**的字符`*` 匹配 0 次或多次，一直匹配到第一个冒号为止 |
|    `:`    |      严格匹配冒号 `:`，对应 HTTP 请求头键值的固定分隔符      |
|   ` ?`    | 匹配 **0 个或 1 个空格**，兼容两种常见格式：- 紧凑格式 `Key:Value`（无空格）- 标准格式 `Key: Value`（有一个空格） |
|  `(.*)`   | 第 2 个捕获组，提取请求头的 **Value（值）**`.` 匹配除换行外的任意字符，`*` 匹配到行尾，把冒号后所有内容都作为值 |
|    `$`    |       行尾锚点，强制匹配到字符串末尾，保证整行符合格式       |

匹配成功则存入header_ 中，失败则切换到请求体解析。

#### 为什么匹配失败就代表请求头结束？

HTTP 协议有强制规定：**请求头和请求体之间，必须用一个空行（仅包含 `\r\n`）分隔**。

带请求体的 POST 请求，完整报文的结构是严格分层的：

```c++
POST /login HTTP/1.1\r\n       ← 第1行：请求行
Host: localhost:1316\r\n       ← ┐
Connection: keep-alive\r\n     ← │
Content-Type: application/x-www-form-urlencoded\r\n  ←  请求头（多行，都是 Key: Value 格式）
Content-Length: 27\r\n         ← ┘
\r\n                           ←  空行！只有 \r\n，是请求头和请求体的强制分界线
username=test&password=123     ←  请求体（POST 表单数据）
```

##### 结构规则（HTTP 协议强制规定）

1. 第一行永远是请求行（方法 + 路径 + 版本）；
2. 接下来是若干行请求头，每行都是 `Key: Value` 格式；
3. **所有请求头结束后，必须跟一个空行（仅 `\r\n`）**，用来标记「请求头到此为止」；
4. 空行之后才是请求体，没有请求体的请求（比如 GET），空行之后就没有内容了。

##### 状态机是怎么流转的

结合上层 `parse` 函数的循环逻辑，我们按行走一遍解析流程，你就能清晰看到 `state_` 什么时候变、为什么变。

###### 阶段 1：解析请求行

- 初始状态：`state_ = REQUEST_LINE`
- 处理第一行 `POST /login HTTP/1.1`
- `ParseRequestLine_` 解析成功，**自动把 `state_` 改成 `HEADERS`**
- 消费掉这一行，进入下一轮循环

###### 阶段 2：逐行解析请求头（状态保持 HEADERS）

接下来的 `Host`、`Connection`、`Content-Type`、`Content-Length` 这几行，全部走同一个逻辑：

1. 切出一行文本，比如 `Host: localhost:1316`；
2. 进入 `case HEADERS` 分支，调用 `ParseHeader_(line)`；
3. 行内容是标准的 `Key: Value` 格式，**正则匹配成功**，把键值对存入 `header_`；
4. 状态不变化，`state_` 还是 `HEADERS`；
5. 消费掉这一行，继续下一轮循环，处理下一个请求头。

> 这个阶段所有行都能匹配正则，不会进 else，状态一直保持 HEADERS，直到遇到空行。

###### 阶段 3：遇到空行，触发状态切换

当解析到那个只有 `\r\n` 的空行时：

1. 去掉 `\r\n` 后，`line` 是**空字符串** `""`；
2. 调用 `ParseHeader_("")`，空字符串显然不满足 `Key: Value` 的正则格式，**正则匹配失败**；
3. 进入 `else` 分支，执行 `state_ = BODY`；
4. 这一步就代表：**所有请求头已经解析完毕，接下来该处理请求体了**。

###### 阶段 4：解析请求体

下一轮 `parse` 循环时，`state_` 已经是 `BODY` 了，就会自动进入 `case BODY` 分支，调用 `ParseBody_` 去解析空行后面的表单数据。

---

所以当解析到这个空行时，传入的 `line` 就是空字符串，无法匹配 `Key: Value` 的正则规则，就会进入 else 分支。

此时就代表所有请求头已经解析完毕，接下来要处理请求体了，所以把状态机的状态从 `HEADERS` 修改为 `BODY`。下一轮 `parse` 函数的循环，就会自动进入 BODY 分支，调用 `ParseBody_` 处理请求体。

### 请求体解析

```c++
/**
 * @brief 解析请求体
 *
 * 将 body 字符串保存，并解析 POST 参数
 *
 * @param line 请求体字符串
 */
void HttpRequest::ParseBody_(const string &line)
{
    body_ = line;
    ParsePost_();
    state_ = FINISH;
    LOG_DEBUG("Body:%s, len:%d", line.c_str(), line.size());
}
```

将传入的子串拷贝给body_ 调用ParsePost_ 解析POST参数。最后将解析状态改为FINISH

### POST 参数解析

```c++
/**
 * @brief 解析 POST 请求
 *
 * 如果是 POST 请求且 Content-Type 为 application/x-www-form-urlencoded，
 * 解析表单数据并验证用户
 *
 * 验证逻辑：
 * - 注册（isLogin=false）：检查用户名是否存在，不存在则插入
 * - 登录（isLogin=true）：检查用户名和密码是否匹配
 *
 * 验证成功后，路径重定向到 welcome.html 或 error.html
 */
void HttpRequest::ParsePost_() {
    if(method_ == "POST" && header_["Content-Type"] == "application/x-www-form-urlencoded") {
        // 解析 URL 编码的表单数据
        ParseFromUrlencoded_();

        // 检查是否是注册或登录请求
        if(DEFAULT_HTML_TAG.count(path_)) {
            int tag = DEFAULT_HTML_TAG.find(path_)->second;
            LOG_DEBUG("Tag:%d", tag);

            if(tag == 0 || tag == 1) {
                bool isLogin = (tag == 1);  // tag=1 表示登录，tag=0 表示注册

                // 验证用户
                if(UserVerify(post_["username"], post_["password"], isLogin)) {
                    path_ = "/welcome.html";  // 验证成功
                }
                else {
                    path_ = "/error.html";   // 验证失败
                }
            }
        }
    }
}
```

这个 `ParsePost_` 是 POST 请求的**业务分发与处理函数**，在 `ParseBody_` 解析完请求体原始内容后被调用，筛选出「表单格式的登录 / 注册 POST 请求」，解析表单参数后调用数据库做账号校验，最后根据校验结果改写请求路径，控制最终返回给客户端的页面。

##### 在入口条件处筛选可处理的 POST 请求：

只有同时满足两个条件，才会进入后续处理，否则函数直接跳过、不做任何操作：

- **条件 1：请求方法是 POST**

  只有 POST 请求才会携带请求体表单数据，GET 请求没有请求体，直接跳过。

- **条件 2：Content-Type 是表单编码格式**

  `application/x-www-form-urlencoded` 是浏览器原生表单默认的编码格式，也是最常见的 POST 提交格式。

  只有这种格式才适配后续的 URL 解码逻辑；如果是文件上传的 `multipart/form-data`、JSON 等其他格式，都不进入这个分支。

调用ParseFromUrlencoded_ 函数，解析表单字符串数据作为键值对到 post_ 哈希表中 。

##### 判断是否为登录 / 注册业务请求

DEFAULT_HTML_TAG是静态常量映射表(unordered_map)，作用是**硬编码标记需要做用户验证的业务路径**：

- 路径是 `/register.html` → 标签为 0 → 注册请求
- 路径是 `/login.html` → 标签为 1 → 登录请求

**逻辑说明**

1. 先判断当前请求路径在不在这个表里：不在就说明不是登录 / 注册请求，不做任何业务处理，函数直接结束；
2. 在表里就取出对应的数字标签，再转成语义更清晰的布尔变量 `isLogin`，提升代码可读性。

##### 用户验证 + 路径改写

调用数据库验证函数UserVerify

`UserVerify` 是类的静态成员函数，负责操作数据库完成校验：

- **登录模式（`isLogin=true`）**：查询用户表，比对用户名和密码是否匹配
- **注册模式（`isLogin=false`）**：查询用户名是否已存在，不存在则插入新用户
- 返回 `true` 表示验证 / 注册成功，返回 `false` 表示失败（密码错误、用户名已存在等）

最后根据UserVerify结果修改`path_`地址。

**通过内部改写请求路径，控制最终返回的页面**。

上层的响应模块会根据最终的 `path_` 去读取对应的 HTML 文件返回给客户端：

- 验证成功 → 路径改为 `/welcome.html` → 返回欢迎页面
- 验证失败 → 路径改为 `/error.html` → 返回错误页面

这种「内部路径改写」的方式，不需要给客户端发 302 重定向，少一次网络交互，逻辑简单直接。

```bash
进入函数
    ↓
判断：是POST请求 + 表单编码格式？
    ├─ 否：函数直接结束，不做处理
    └─ 是：调用 ParseFromUrlencoded_() 解码表单，存入 post_
            ↓
            判断：当前路径是登录/注册页？
                ├─ 否：函数结束
                └─ 是：根据路径标记区分登录/注册
                        ↓
                        调用 UserVerify 走数据库校验
                            ├─ 成功：path_ 改为 /welcome.html
                            └─ 失败：path_ 改为 /error.html
```



### 从 URL 编码解析表单数据

```c++
/**
 * @brief 从 URL 编码解析表单数据
 *
 * 处理：
 * - '=' 分隔键值对，提取 key
 * - '+' 替换为空格
 * - '%XX' 解码为字符（如 %20 -> 空格）
 * - '&' 分隔多个键值对，提取 value
 *
 * 示例：username=john+doe&password=123
 * 解析后：{"username": "john doe", "password": "123"}
 */
void HttpRequest::ParseFromUrlencoded_()
{
    if (body_.size() == 0)
    {
        return;
    }

    string key, value;
    int num = 0;
    int n = body_.size(); //请求体长度
    int i = 0, j = 0;
    //i是遍历索引，j当前键或值片段的起始索引
    for (; i < n; i++)
    {
        char ch = body_[i];
        switch (ch)
        {
        case '=':
            //遇到'='，从j到i-1截取，提取key
            key = body_.substr(j, i - j);
            j = i + 1; //跳过=号
            break;
        //'+'替换为空格
        case '+':
            body_[i] = ' ';
            break;
        case '%':
            //URL解码：%XX ->字符
            num = ConverHex(body_[i + 1]) * 16 + ConverHex(body_[i + 2]);
            //body_[i + 1] = num % 10 + '0';
            //body_[i + 1] = num / 10 + '0';

            // 修正：把解码后的字符写入当前位置，后续遍历会自动读取
            body_[i] = num;
            i += 2;
            break;
        case '&':
            //遇到'&'，提取value
            value = body_.substr(j, i - j);
            j = i + 1;
            //存储键值对
            post_[key] = value;
            LOG_DEBUG("%s = %s", key.c_str(), value.c_str());
            break;

        default:
            break;
        }
    }

    //最后一个键值对后面没有 &,不会触发上面的 case '&' 分支，所以循环结束后需要单独处理

    assert(j <= i);
    if (post_.count(key) == 0 && j < i)
    {
        value = body_.substr(j, i - j);
        post_[key] = value;
    }
}
```

`ParseFromUrlencoded_` 是 POST 表单解析的核心解码函数，专门处理浏览器原生表单默认的编码格式 `application/x-www-form-urlencoded`。

#### 为什么需要这个解码？

浏览器提交表单时，会对特殊字符做统一编码：

1. 空格会被编码为 `+`
2. 所有特殊字符（中文、符号、不可见字符）会被编码为 `%XX` 格式，XX 是字符的 ASCII 十六进制值
3. 多个键值对用 `&` 分隔，键和值用 `=` 分隔

比如输入用户名 `john doe`、密码 `123`，最终请求体原始内容是：

```bash
username=john+doe&password=123
```

这个函数的作用就是**把编码后的原始字符串，解码还原成正常的键值对，存入 `post_` 哈希表**，供后续登录 / 注册校验读取参数。

进入后先请求体判空，避免无效遍历。

这里采用**单遍双指针**的高效设计来遍历请求体，时间复杂度 O (n)，一次遍历完成所有解析：

- `i`：遍历指针，逐个字符往前走，遍历整个请求体
- `j`：片段起始指针，标记当前 key 或 value 的起始位置
- 遇到分隔符时，直接用 `substr(j, i-j)` 截取出内容，不需要额外分割字符串，效率更高

#### for+switch遍历

逐个字符处理，根据字符类型进入对应分支：

##### ① 遇到 `=`：截取 key

```c++
case '=':
    key = body_.substr(j, i - j);
    j = i + 1; // 跳过=号，j指向value的起始位置
    break;
```

`=` 是键和值的分隔符，走到这里说明**key 已经读取完毕**：

- 从片段起始 `j` 到当前位置 `i`，截取出 key（如 `username`）
- 把 `j` 后移一位，跳过 `=`，接下来的内容就是 value 的起始

##### ② 遇到 `+`：替换为空格

```c++
case '+':
    body_[i] = ' ';
    break;
```

URL 编码规范：空格会被编码为 `+`，直接原地替换为空格即可。

##### ③ 遇到 `%`：URL 十六进制解码

```c++
case '%':
    // URL解码：%XX -> 对应ASCII字符
    num = ConverHex(body_[i + 1]) * 16 + ConverHex(body_[i + 2]);
    body_[i + 1] = num % 10 + '0';
    body_[i + 1] = num / 10 + '0';
    i += 2;
    break;
```

`%` 是 URL 转义标记，后面紧跟**两位十六进制数**，对应字符的 ASCII 码。比如 `%20` 就是十六进制 0x20 = 十进制 32，对应空格字符。

正确流程应该是:

1. 取 `%` 后两位字符，转成十六进制数值，计算出对应的 ASCII 码
2. 把解码后的字符替换到当前位置，跳过后面两位

但是这里

##### ④ 遇到 `&`：截取 value，保存键值对

```c++
case '&':
    value = body_.substr(j, i - j);
    j = i + 1;
    post_[key] = value;
    LOG_DEBUG("%s = %s", key.c_str(), value.c_str());
    break;
```

`&` 是键值对的分隔符，走到这里说明**当前键值对读取完毕**：

- 从 `j` 到 `i` 截取出 value
- `j` 后移一位，准备下一个 key 的起始

------

**处理最后一个键值对**

```c++
assert(j<=i);
if(post_.count(key) == 0&&j<i){
    value = body_.substr(j,i-j);
    post_[key] = value;
}
```

最后一个键值对后面没有 `&`，不会触发上面的 `case '&'` 分支，所以循环结束后需要单独处理：

- 截取最后一个 value
- 存入 `post_` 哈希表
- 加 `assert` 保证索引不越界，加 `count(key)==0` 避免重复覆盖

------

##### 完整解析流程示例

我们拿示例 `username=john+doe&password=123` 走一遍完整流程：

|  i 位置  |  字符  |   分支   |                操作                 |
| :------: | :----: | :------: | :---------------------------------: |
|   0-7    | u 到 e | default  |         正常遍历，j=0 不动          |
|    8     |   =    | case '=' |    截取 [0,8) 得 `username`，j=9    |
|   9-12   | j 到 n | default  |              正常遍历               |
|    13    |   +    | case '+' |  替换为空格，字符串变成 `john doe`  |
|  14-16   | d 到 e | default  |              正常遍历               |
|    17    |   &    | case '&' |   截取 [9,17) 得 `john doe`，j=18   |
|  18-25   | p 到 d | default  |              正常遍历               |
| 循环结束 |   -    |    -     | 截取 [18,26) 得 `123`，存入 `post_` |

-----

### 用户验证

```c++
/**
 * @brief 验证用户（注册/登录）
 *
 * 通过数据库验证用户名和密码
 *
 * 注册逻辑（isLogin=false）：
 * 1. 检查用户名是否已存在
 * 2. 如果不存在，插入新用户
 *
 * 登录逻辑（isLogin=true）：
 * 1. 检查用户名和密码是否匹配
 *
 * @param name 用户名
 * @param pwd 密码
 * @param isLogin 是否为登录操作
 * @return bool 验证是否成功
 */
bool HttpRequest::UserVerify(const string &name, const string &pwd, bool isLogin)
{
    if (name == "" || pwd == "")
    {
        return false;
    }
    LOG_INFO("Verify name:%s pwd:%s", name.c_str(), pwd.c_str());

    //获取数据库连接（使用RAII自动释放）
    MYSQL *sql;
    SqlConnRAII(&sql, SqlConnPool::Instance());
    assert(sql);

    bool flag = false;  //最终结果标志位
    unsigned int j = 0; //存储字段数量
    //SQL最长不会超过256字节，且栈数组更快，snprintf更适配
    char order[256] = {0}; //SQL语句缓冲区
    MYSQL_FIELD *fields = nullptr;
    MYSQL_RES *res = nullptr; //查询结果集指针

    //注册时默认成功（如果用户名不存在）
    if (!isLogin)
    {
        flag = true;
    } //用一个初始值适配两个场景

    //查询用户及密码 用snprintf指定最大长度256字节超过截断
    snprintf(order, 256, "SELECT username,password FROM user WHERE username='%s' LIMIT 1", name.c_str());
    LOG_DEBUG("%s", order);

    if (mysql_query(sql, order))
    {
        mysql_free_result(res);
        return false;
    }
    res = mysql_store_result(sql);
    j = mysql_num_fields(res);
    fields = mysql_fetch_fields(res);

    while (MYSQL_ROW row = mysql_fetch_row(res))
    {
        LOG_DEBUG("MYSQL ROW: %s %s", row[0], row[1]);
        string password(row[1]);
        if (isLogin)
        {
            //登陆：检查密码是否匹配
            if (pwd == password)
            {
                flag = true;
            }
            else
            {
                flag = false;
                LOG_DEBUG("pwd error!");
            }
        }
        else
        {
            //注册：用户名已存在
            flag = false;
            LOG_DEBUG("user used!");
        }
    }
    mysql_free_result(res);
    //注册行为且用户名未被使用
    if (!isLogin && flag == true)
    {
        LOG_DEBUG("regirster!");
        bzero(order, 256);
        snprintf(order, 256, "INSERT INTO user(username,password) VALUES('%s','%s')", name.c_str(), pwd.c_str());
        LOG_DEBUG("%s", order);
        if (mysql_query(sql, order))
        {
            LOG_DEBUG("Insert error!");
            flag = false;
        }
            //flag = true;
    }
    //SqlConnPool::Instance()->FreeConn(sql);
    LOG_DEBUG("UserVerify success!!");
    return flag;
}
```











### 信息获取

```c++
std::string HttpRequest::path() const
{
    return path_;
}

std::string &HttpRequest::path()
{
    return path_;
}

std::string HttpRequest::method() const
{
    return method_;
}

std::string HttpRequest::version() const
{
    return version_;
}

//string和C风格版本返回Post的键值对
std::string HttpRequest::GetPost(const std::string &key) const
{
    assert(key != "");
    if (post_.count(key) == 1)
    {
        return post_.find(key)->second;
    }
    return "";
}

std::string HttpRequest::GetPost(const char *key) const
{
    assert(key != nullptr);
    if (post_.count(key) == 1)
    {
        return post_.find(key)->second;
    }
    return "";
}

```

设置对外访问的接口函数，GetPost设置C风格和string两种接收参数。