#include "http_conn.h"
#include <errno.h>
#include <algorithm>

// ====================== 简单工厂 - 抽象产品 ======================
// 读取本地静态文件
bool HttpRequestHandler::readFile(const std::string& fileName, std::string& fileContent) {
    std::string fullPath = std::string(WWW_ROOT) + fileName;
    std::ifstream file(fullPath, std::ios::binary);
    if (!file.is_open()) return false;
    // 快速读取完整文件
    file.seekg(0, std::ios::end);
    int fileSize = file.tellg();
    file.seekg(0, std::ios::beg);
    fileContent.resize(fileSize);
    file.read(&fileContent[0], fileSize);
    file.close();
    return true;
}

// ====================== 简单工厂 - 具体产品 ======================
// GET请求处理：读取静态资源或返回404
std::string GetHandler::handle(HttpConn& conn) {
    std::ostringstream oss;
    std::string fileContent;
    if (readFile(conn.path, fileContent)) {
        oss << "HTTP/1.1 200 OK\r\n";
        oss << "Content-Type: text/html; charset=utf-8\r\n";
        oss << "Content-Length: " << fileContent.size() << "\r\n";
        oss << "Connection: close\r\n";
        oss << "\r\n";
        oss << fileContent;
    } else {
        std::string notFound = "<html><body><h1>404 Not Found</h1><p>静态文件不存在</p></body></html>";
        oss << "HTTP/1.1 404 Not Found\r\n";
        oss << "Content-Type: text/html; charset=utf-8\r\n";
        oss << "Content-Length: " << notFound.size() << "\r\n";
        oss << "Connection: close\r\n";
        oss << "\r\n";
        oss << notFound;
    }
    return oss.str();
}

// POST请求处理：回显请求体数据
std::string PostHandler::handle(HttpConn& conn) {
    std::ostringstream oss;
    std::string postResp = "<html><body><h1>POST Request Success</h1>"
                           "<p>Your POST Data:</p><hr>"
                           "<pre>" + conn.postData + "</pre>"
                           "</body></html>";
    oss << "HTTP/1.1 200 OK\r\n";
    oss << "Content-Type: text/html; charset=utf-8\r\n";
    oss << "Content-Length: " << postResp.size() << "\r\n";
    oss << "Connection: close\r\n";
    oss << "\r\n";
    oss << postResp;
    return oss.str();
}

// ====================== 简单工厂 - 工厂核心 ======================
// 根据请求方法创建对应处理器
std::unique_ptr<HttpRequestHandler> RequestFactory::createHandler(METHOD method) {
    switch (method) {
        case GET:
            return std::unique_ptr<HttpRequestHandler>(new GetHandler());
        case POST:
            return std::unique_ptr<HttpRequestHandler>(new PostHandler());
        default:
            return nullptr;
    }
}

// ====================== HttpConn 核心实现 ======================
// 构造函数：初始化RAII资源
HttpConn::HttpConn() 
    : sockFd(nullptr, [](int* fd) { 
        if (fd && *fd != -1) { 
            close(*fd);
            delete fd;
        } 
    }), 
    parseState(PARSE_REQUESTLINE), 
    readIdx(0), 
    checkIdx(0), 
    startLine(0), 
    contentLength(0), 
    method(GET) { 
    memset(&clientAddr, 0, sizeof(clientAddr)); 
    readBuf.fill('\0'); 
}

// 初始化新连接
void HttpConn::init(int fd, const sockaddr_in& addr) {
    sockFd.reset(new int(fd));
    clientAddr = addr;
    parseState = PARSE_REQUESTLINE;
    readIdx = 0;
    checkIdx = 0;
    startLine = 0;
    contentLength = 0;
    postData.clear();
    method = GET;
    readBuf.fill('\0');
    responseContent.clear();
}

// 非阻塞读取请求数据（ET模式循环读尽）
ssize_t HttpConn::readData() {
    ssize_t len = 0;
    ssize_t totalLen = 0;
    while (true) {
        len = recv(*sockFd, readBuf.data() + readIdx, READ_BUFFER_SIZE - readIdx - 1, 0);
        if (len < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) break;
            return -1;
        } else if (len == 0) {
            return 0;
        }
        readIdx += len;
        totalLen += len;
    }
    return totalLen;
}

// 关闭连接并释放资源
void HttpConn::closeConn(int epollFd) {
    EpollUtil::delFd(epollFd, *sockFd);
    sockFd.reset();
}

// 获取当前解析行指针
char* HttpConn::getLine() {
    return readBuf.data() + startLine;
}

// 从状态机：按\r\n切割单行
LINE_STATE HttpConn::parseLine() {
    char temp;
    for (; checkIdx < readIdx; checkIdx++) {
        temp = readBuf[checkIdx];
        if (temp == '\r') {
            if (checkIdx + 1 == readIdx) return LINE_OPEN;
            else if (readBuf[checkIdx + 1] == '\n') {
                readBuf[checkIdx++] = '\0';
                readBuf[checkIdx++] = '\0';
                return LINE_OK;
            }
            return LINE_BAD;
        } else if (temp == '\n') {
            if (checkIdx >= 1 && readBuf[checkIdx - 1] == '\r') {
                readBuf[checkIdx - 1] = '\0';
                readBuf[checkIdx++] = '\0';
                return LINE_OK;
            }
            return LINE_BAD;
        }
    }
    return LINE_OPEN;
}

// 解析HTTP请求行
HTTP_CODE HttpConn::parseRequestLine(const char* line) {
    char methodStr[16], pathStr[256], versionStr[16];
    if (sscanf(line, "%s %s %s", methodStr, pathStr, versionStr) != 3) return BAD_REQUEST;
    
    if (strcasecmp(methodStr, "GET") == 0) method = GET;
    else if (strcasecmp(methodStr, "POST") == 0) method = POST;
    else return BAD_REQUEST;

    path = (strcmp(pathStr, "/") == 0) ? "/index.html" : pathStr;
    version = versionStr;
    parseState = PARSE_HEADER;
    return NO_REQUEST;
}

// 解析HTTP请求头（仅处理Content-Length）
HTTP_CODE HttpConn::parseHeader(const char* line) {
    if (line[0] == '\0' || line[0] == '\r' || line[0] == '\n') {
        if (method == POST && contentLength > 0) {
            parseState = PARSE_BODY;
            return NO_REQUEST;
        } else {
            parseState = PARSE_DONE;
            return GET_REQUEST;
        }
    }
    if (strncasecmp(line, "Content-Length:", 15) == 0) {
        const char* lenStr = line + 15;
        while (*lenStr == ' ' || *lenStr == '\t') lenStr++;
        contentLength = atoi(lenStr);
    }
    return NO_REQUEST;
}

// 解析POST请求体
HTTP_CODE HttpConn::parseBody() {
    int bodyReadLen = readIdx - checkIdx;
    if (bodyReadLen < contentLength) return NO_REQUEST;
    else {
        postData = std::string(readBuf.data() + checkIdx, contentLength);
        checkIdx += contentLength;
        parseState = PARSE_DONE;
        return GET_REQUEST;
    }
}

// 主状态机：整体解析HTTP请求
HTTP_CODE HttpConn::parseHttpRequest() {
    LINE_STATE lineState = LINE_OK;
    HTTP_CODE ret = NO_REQUEST;
    
    while (true) {
        if (parseState == PARSE_BODY) {
            ret = parseBody();
            if (ret == GET_REQUEST) return GET_REQUEST;
            else if (ret == NO_REQUEST) return NO_REQUEST;
            else return BAD_REQUEST;
        }
        lineState = parseLine();
        if (lineState == LINE_OK) {
            char* line = getLine();
            startLine = checkIdx;
            switch (parseState) {
                case PARSE_REQUESTLINE:
                    ret = parseRequestLine(line);
                    if (ret == BAD_REQUEST) return BAD_REQUEST;
                    break;
                case PARSE_HEADER:
                    ret = parseHeader(line);
                    if (ret == BAD_REQUEST) return BAD_REQUEST;
                    else if (ret == GET_REQUEST) return GET_REQUEST;
                    break;
                default:
                    return INTERNAL_ERROR;
            }
        } else if (lineState == LINE_BAD) {
            return BAD_REQUEST;
        } else {
            return NO_REQUEST;
        }
    }
}

// 生成HTTP响应
bool HttpConn::makeResponse(HTTP_CODE code) {
    std::ostringstream oss;
    // 处理请求解析错误
    if (code != GET_REQUEST) {
        std::string errHtml;
        if (code == BAD_REQUEST) {
            errHtml = "<html><body><h1>400 Bad Request</h1><p>请求格式非法</p></body></html>";
            oss << "HTTP/1.1 400 Bad Request\r\n";
        } else {
            errHtml = "<html><body><h1>500 Internal Server Error</h1><p>服务器异常</p></body></html>";
            oss << "HTTP/1.1 500 Internal Server Error\r\n";
        }
        oss << "Content-Type: text/html; charset=utf-8\r\n";
        oss << "Content-Length: " << errHtml.size() << "\r\n";
        oss << "Connection: close\r\n\r\n";
        oss << errHtml;
        responseContent = std::move(oss.str());
        return true;
    }
    // 简单工厂：创建对应处理器并生成响应
    auto handler = RequestFactory::createHandler(method);
    if (handler != nullptr) {
        responseContent = std::move(handler->handle(*this));
    }
    return !responseContent.empty();
}

// 业务处理入口
void HttpConn::processRequest() {
    HTTP_CODE code = parseHttpRequest();
    makeResponse(code);
}

// 非阻塞发送响应
ssize_t HttpConn::sendResponse() {
    if (responseContent.empty()) return -1;
    ssize_t len = send(*sockFd, responseContent.c_str(), responseContent.size(), 0);
    if (len > 0) responseContent.clear();
    return len;
}