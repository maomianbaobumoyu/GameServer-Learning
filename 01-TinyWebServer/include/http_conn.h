#ifndef HTTP_CONN_H
#define HTTP_CONN_H
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <array> 
#include <memory> 
#include <functional> 
#include "epoll_util.h"

extern const char* WWW_ROOT;
class HttpConn;  // 解决循环依赖

// HTTP请求方法
enum METHOD {GET = 0, POST, HEAD, PUT, DELETE, TRACE, OPTIONS, CONNECT};
// HTTP响应状态码
enum HTTP_CODE {NO_REQUEST, GET_REQUEST, BAD_REQUEST, NO_RESOURCE, FORBIDDEN, FILE_REQUEST, INTERNAL_ERROR, CLOSED_CONNECTION};
// 主状态机状态
enum PARSE_STATE {PARSE_REQUESTLINE, PARSE_HEADER, PARSE_BODY, PARSE_DONE};
// 从状态机状态
enum LINE_STATE {LINE_OK, LINE_BAD, LINE_OPEN};

/**
 * @brief 抽象请求处理器基类（简单工厂模式）
 */
class HttpRequestHandler {
public:
    virtual ~HttpRequestHandler() = default;
    // 统一请求处理接口
    virtual std::string handle(HttpConn& conn) = 0;
protected:
    // 读取本地静态文件
    bool readFile(const std::string& fileName, std::string& fileContent);
};

// GET请求处理器
class GetHandler : public HttpRequestHandler {
public:
    std::string handle(HttpConn& conn) override;
};

// POST请求处理器
class PostHandler : public HttpRequestHandler {
public:
    std::string handle(HttpConn& conn) override;
};

/**
 * @brief 请求处理器工厂
 */
class RequestFactory {
public:
    // 根据请求方法创建对应处理器
    static std::unique_ptr<HttpRequestHandler> createHandler(METHOD method);
};

/**
 * @brief HTTP连接类：封装单个客户端的请求-响应全流程
 */
class HttpConn {
    friend class HttpRequestHandler;
    friend class GetHandler;
    friend class PostHandler;
public:
    static const int READ_BUFFER_SIZE = 8192;
    
    std::array<char, READ_BUFFER_SIZE> readBuf;  // 读缓冲区
    sockaddr_in clientAddr;  // 客户端地址
    std::unique_ptr<int, std::function<void(int*)>> sockFd;  // 客户端socket（RAII管理）

    HttpConn();
    ~HttpConn() = default;

    // 初始化新连接
    void init(int fd, const sockaddr_in& addr);
    // 非阻塞读取请求数据
    ssize_t readData();
    // 业务处理入口：解析请求+生成响应
    void processRequest();
    // 非阻塞发送响应
    ssize_t sendResponse();
    // 关闭连接并释放资源
    void closeConn(int epollFd);
private:
    int readIdx;        // 已读数据长度
    int checkIdx;       // 当前解析位置
    int startLine;      // 当前解析行起始位置
    PARSE_STATE parseState;  // 主状态机状态
    LINE_STATE lineState;    // 从状态机状态

    METHOD method;      // 请求方法
    std::string path;   // 请求路径
    std::string version;// HTTP版本
    std::string responseContent;  // 响应报文
    int contentLength;  // POST请求体长度
    std::string postData;  // POST请求体

    // 主状态机：解析HTTP请求
    HTTP_CODE parseHttpRequest();
    // 从状态机：按\r\n切割单行
    LINE_STATE parseLine();
    // 获取当前解析行指针
    char* getLine();
    // 解析请求行
    HTTP_CODE parseRequestLine(const char* line);
    // 解析请求头
    HTTP_CODE parseHeader(const char* line);
    // 解析POST请求体
    HTTP_CODE parseBody();
    // 生成HTTP响应
    bool makeResponse(HTTP_CODE code);
};
#endif