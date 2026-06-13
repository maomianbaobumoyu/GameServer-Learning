这个函数的核心作用是**判断当前 HTTP 请求是否需要保持长连接（Keep-Alive）**。它是一个纯查询逻辑的常成员函数，末尾的 `const` 保证函数全程只读取成员变量、不会修改对象状态，符合状态查询类接口的设计规范。

------

## 一、前置说明：Connection 信息从哪里来？

调用 `IsKeepAlive()` 时，HTTP 请求已经完成了解析流程，`Connection` 头信息早已被存入类的成员变量中：

1. 状态机流转到 `HEADERS` 阶段时，会逐行调用 `ParseHeader_()` 解析请求头；
2. `ParseHeader_()` 通过正则匹配 `Key: Value` 格式，把每一个请求头的「字段名」作为 key、「字段值」作为 value，存入私有成员 `header_`（`unordered_map` 类型的哈希表）。

比如客户端发来请求头：

plaintext









```
Connection: keep-alive
```

解析后 `header_` 里就会存入键值对：`{"Connection", "keep-alive"}`。

所以 `IsKeepAlive()` 并不是实时解析原始报文，只是**从已经解析完成的请求头哈希表中读取数据做逻辑判断**。

------

## 二、逐行拆解代码逻辑

cpp



运行







```
bool HttpRequest::IsKeepAlive() const {
    if(header_.count("Connection") == 1) {
        return header_.find("Connection")->second == "keep-alive" && version_ == "1.1";
    }
    return false;
}
```

### 1. 前置校验：判断是否存在 Connection 请求头

cpp



运行







```
if(header_.count("Connection") == 1)
```

- ```
  header_
  ```

   是键唯一的哈希表，

  ```
  count(key)
  ```

   用于统计键的出现次数：

  - 返回 `1`：请求中包含 `Connection` 这个请求头；
  - 返回 `0`：客户端未发送该请求头。

- 这一步是兜底判断：如果连 `Connection` 头都不存在，直接判定为非长连接，返回 `false`。

### 2. 核心判断：双条件同时满足才判定为长连接

cpp



运行







```
return header_.find("Connection")->second == "keep-alive" && version_ == "1.1";
```

通过 `&&` 逻辑与连接，**两个条件必须同时成立，才返回 true（长连接）**：

#### 条件 1：Connection 头的值为 keep-alive

- `header_.find("Connection")` 返回指向该键值对的迭代器；
- `->second` 取出该键对应的 value（即请求头的具体内容）；
- 字符串比对，判断值是否为 `"keep-alive"`。

#### 条件 2：HTTP 协议版本为 1.1

- `version_` 是解析请求行时提取并保存的 HTTP 版本号（如 `"1.1"`）；
- 协议背景补充：
  - HTTP/1.0 默认是短连接，需要显式加 `Connection: keep-alive` 才会开启长连接；
  - HTTP/1.1 默认就是长连接，即使不写 `Connection` 头也默认保持连接。

> 补充：本项目做了保守实现 ——**必须同时满足「显式携带 keep-alive 头 + 版本为 1.1」才判定为长连接**，和 HTTP/1.1 默认长连接的标准协议规则略有区别，是项目自定义的实现逻辑。

### 3. 兜底返回

不满足上述任一条件时，最终返回 `false`，表示当前为短连接，处理完请求后即可关闭连接。

------

## 三、常见场景返回结果示例

表格







|              请求场景               | 返回值 |                原因                |
| :---------------------------------: | :----: | :--------------------------------: |
| HTTP/1.1 + `Connection: keep-alive` |  true  |          两个条件全部满足          |
|    HTTP/1.1 + 未写 Connection 头    | false  | 代码要求必须显式携带 Connection 头 |
| HTTP/1.0 + `Connection: keep-alive` | false  |           版本不满足 1.1           |
|   HTTP/1.1 + `Connection: close`    | false  |    Connection 值不是 keep-alive    |