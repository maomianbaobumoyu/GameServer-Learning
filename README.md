# GameServer-Learning
从零开始学习C++游戏服务器开发，目标进入游戏厂做C++游戏服务器开发。
本仓库记录完整的学习轨迹、代码实现、学习笔记和项目迭代过程。

## 个人背景
- 大二软件工程专业
- 已完成：SGI STL高性能内存池
- 当前进度：L1基础夯实 - spdlog工业级日志库集成
- 个人CSDN博客：https://blog.csdn.net/2302_78913144?spm=1000.2115.3001.5343

## 学习路线
### 第一阶段：L1基础夯实（现在进行中）
用工业级标准项目搭建游戏服务器底层骨架
- [x] 项目1：手写SGI STL高性能内存池 + 集成测试
- [ ] 组件1：spdlog工业级日志库集成
- [ ] 项目2：手写Reactor高并发网络库
- [ ] 组件2：Protobuf自定义二进制协议

### 第二阶段：L2游戏服核心
在底层框架基础上，实现完整玩家数据管理和基础游戏业务
- [ ] 项目3：多线程高性能服务器
- [ ] 组件3：MySQL数据库（数据持久化）
- [ ] 组件4：Redis缓存（高并发热点数据）
- [ ] 项目4：简易内存缓存系统
- [ ] 游戏业务Demo1：Protobuf多人聊天服务器
- [ ] 游戏业务Demo2：简易游戏登录服务器

### 第三阶段：L3分布式进阶
掌握现代游戏服务器分布式架构核心技术
- [ ] 项目5：用户态协程库
- [ ] 项目6：基于Protobuf的RPC远程调用框架
- [ ] 组件5：Kafka消息队列

### 第四阶段：简历冲刺优化
打磨项目细节，补充工程化能力
- [ ] 组件6：Docker容器化部署
- [ ] 组件7：Nginx反向代理（了解）
- [ ] 项目整体性能压测与优化

## 项目目录

GameServer-Learning/

├── 00-Notes/                    # 所有学习笔记（按科目 / 阶段分类）

│   ├── C++/

│   ├── Linux/

│   ├── Network/

│   ├── MySQL/

│   └── Redis/

├── 01-TinyWebServer/            # 已完成的基础 HTTP 服务器

├── 02-MemoryPool/               # 内存池项目

├── 03-ThreadPool/               # 线程池项目

├── 04-Network-Library/          # 手写网络库

├── 05-HighPerformance-Server/   # 高性能多线程服务器

├── 06-MySQL-Redis/              # 数据库与缓存集成

├── 07-Simple-Cache/             # 简易内存缓存系统

├── 08-Game-Demo/                # 游戏业务 Demo（聊天 / 登录）

├── 09-Coroutine/                # 协程库实现

├── 10-RPC/            # RPC 远程调用框架

├── 11-Kafka/        # Kafka 消息队列集成

└── assets/                      # 架构图、截图等资源

## 提交规范
- `feat: 新增XX功能/模块`  例如：feat: 实现内存池allocate与deallocate核心函数
- `fix: 修复XXbug`  例如：fix: 修复内存池chunk_alloc时的内存泄漏问题
- `docs: 添加/更新XX学习笔记/文档`  例如：docs: 添加SGI内存池设计思想学习笔记
- `test: 添加XX测试代码`  test: 添加内存池性能对比测试
- `refactor: 重构XX模块代码`  例如：refactor: 重构内存池空闲链表管理逻辑
- `chore: 工程化配置/依赖更新`  例如：chore: 添加CMake编译配置文件