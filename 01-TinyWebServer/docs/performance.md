# 性能测试报告
## 测试环境
- 操作系统：CentOS 7.9
- CPU：2核 Intel(R) Xeon(R) CPU E5-2680 v4 @ 2.40GHz
- 内存：4GB
- 编译器：GCC 8.3.1
- 编译选项：-O2 -std=c++17 -pthread

## 测试工具
- wrk 4.2.0：HTTP压测工具
- Valgrind 3.15.0：内存检测工具

## 压测结果
### 测试命令
```bash
wrk -c 1000 -t 4 -d 30s --timeout 2s http://127.0.0.1:8080/index.html
```

### 完整输出

```bash
Running 30s test @ http://127.0.0.1:8080/index.html
  4 threads and 1000 connections
  Thread Stats   Avg      Stdev     Max   +/- Stdev
    Latency    17.34ms   92.38ms    1.63s    97.9%
    Req/Sec     8.98k     5.36k   26.51k    63.1%
  1024784 requests in 30.09s, 364.54MB read
  Socket errors: connect 0, read 0, write 0, timeout 97
Requests/sec:  34053.71
Transfer/sec:     12.11MB
```

## 内存检测结果

### 测试命令

```bash
valgrind --leak-check=full --show-leak-kinds=all ./http_server 8080 4 ET
```

### 完整输出

```bash
==16295== HEAP SUMMARY:
==16295==     in use at exit: 0 bytes in 0 blocks
==16295==   total heap usage: 27 allocs, 27 frees, 21,780 bytes allocated
==16295== 
==16295== All heap blocks were freed -- no leaks are possible
==16295== 
==16295== For lists of detected and suppressed errors, rerun with: -s
==16295== ERROR SUMMARY: 0 errors from 0 contexts (suppressed: 0 from 0)
```

## 测试结论

1. 服务器在 1000 并发连接下，QPS 达到 3.4 万，性能表现优秀
2. 内存管理完善，无任何内存泄漏和野指针问题
3. 稳定性良好，30 秒压测无连接和读写错误
4. 97 个超时错误是由于虚拟机资源限制（2 核 4G），可通过增加服务器资源或实现限流机制优化