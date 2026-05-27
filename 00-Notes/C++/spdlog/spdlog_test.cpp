#include<cstdlib>
#include<iostream>
#include<spdlog/spdlog.h>
#include<spdlog/sinks/stdout_sinks.h>
#include<spdlog/sinks/basic_file_sink.h>
#include<spdlog/sinks/stdout_color_sinks.h>
#include<spdlog/sinks/rotating_file_sink.h>
#include<chrono>
#include <spdlog/async.h>
//#include<spdlog/sinks/win_eventlog_sink.h>
//我在Linux环境，无windows.h

//第一个spdlog程序
void func1()
{
    char const* lib7="spd_hello";
    spdlog::info("hello {}",lib7);
}

//替换全局日志记录器
void func2()//替换全局日志记录器
{
    spdlog::info("替换之前info，带颜色");
    spdlog::warn("这是警告，带颜色");

    //创建新的记录器（不带颜色的标准输出）
    auto colorlessLogger = spdlog::stdout_logger_mt("Colorless");
    spdlog::set_default_logger(colorlessLogger);
    spdlog::info("替换之后info，不带颜色");
    spdlog::warn("这是警告，不带颜色");  
}


void functionNeedFileLogger()
{
    auto logger = spdlog::get("FileLogger");
    assert(logger);
    logger->info("{} 函数中，通过名字是{}的记录器，输出本日志",__FUNCTION__,logger->name());
}


//使用文件记录器
void func3()
{
    spdlog::info("下面内容，只输出到文件日志中");
    auto fileLogger = spdlog::basic_logger_mt("FileLogger","log/file_log.txt");
    fileLogger->warn("这是一个警告");

    spdlog::error("这个是屏幕error");

    functionNeedFileLogger();
}

//重定向标准输出
void func4()
{
    //创建标准输出（带彩色）槽
    auto stdoutSink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();

    //创建一个标准错误输出（带彩色）槽
    auto stderrSink = std::make_shared<spdlog::sinks::stderr_color_sink_mt>();

    //设置 stderr 槽只输出warn及以上级别的日志（在挂接之前设置好）
    stderrSink->set_level(spdlog::level::warn);

    //获取默认的日志记录器，并清空它原有的槽
    auto defaultLogger = spdlog::default_logger();
    defaultLogger->sinks().clear();

    //将上面的新创建的槽挂接到默认记录器：
    defaultLogger->sinks().push_back(stdoutSink);
    defaultLogger->sinks().push_back(stderrSink);

    spdlog::info("1.info");
    spdlog::error("2.error");
    spdlog::info("3.info");
    spdlog::critical("4.critical");


}

//多个记录器
void func5()
{
   spdlog::info("全局日志记录器将新增回滚编号文件槽");

   //1.全局日志记录器 - 颜色控制台 + 回滚编号文件槽
   auto rotatingFileSink = std::make_shared<spdlog::sinks::rotating_file_sink_mt>("log/main-rotating.txt",1024*1024*5,9);
   //取默认记录器
   auto defaultLogger = spdlog::default_logger();
   defaultLogger->sinks().push_back(rotatingFileSink);//加入新槽

   spdlog::info("全局日志记录器已经添加回滚编号文件槽");

   //2.专用于监控业务的日志记录器
   auto colornessOutSink = std::make_shared<spdlog::sinks::stdout_sink_mt>();

   //创建普通文件槽
   auto fileSink = std::make_shared<spdlog::sinks::basic_file_sink_mt>("log/monitor.txt");

   #ifdef _WIN32
   //创建windowsOS的时间记录槽
   auto winEvtSink=std::make_shared<spdlog::sinks::win_eventlog_sink_mt>("HelloSpdlog");
   #endif
  
   //创建一个全新的日志记录器
   auto monitorLogger = std::make_shared<spdlog::logger>("MonitorLogger");
   monitorLogger->sinks().push_back(colornessOutSink);
   monitorLogger->sinks().push_back(fileSink);

   #ifdef _WIN32
    monitorLogger->sinks().push_back(winEvtSink);
   #endif

   monitorLogger->info("监控日志有{}个槽",monitorLogger->sinks().size());

}

//日志级别调整
void func6()
{
    int const IDX_CONSOLE_SINK = 0;//控制台槽的下标
    int const IDX_FILE_SINK = 1;//文件槽的下标
    
    //使用工厂方法创建全新的日志记录器
    auto levelsLogger = spdlog::stdout_color_mt("LevelsLogger");
    //创建文件槽
    auto fileSink = std::make_shared<spdlog::sinks::basic_file_sink_mt>("log/levels.txt");
    //修改文件槽的级别
    fileSink->set_level(spdlog::level::warn);
    //加入到记录器
    levelsLogger->sinks().push_back(fileSink);

    //查看记录器的级别
    levelsLogger->info("LevelsLogger 记录器级别 :{}",spdlog::level::to_string_view(levelsLogger->level()));

    //查看各个槽的级别
    for(auto sink:levelsLogger->sinks())
    {
        levelsLogger->info(spdlog::level::to_short_c_str(sink->level()));
    }

    levelsLogger->info("this is info ");
    levelsLogger->debug("this is debug");

    //先调整记录器的级别
    levelsLogger->set_level(spdlog::level::debug);
    levelsLogger->debug("本记录器等级已经调整为debug");
    levelsLogger->warn("debug 和 info 级别日志不会输出到文件");

    levelsLogger->sinks()[IDX_FILE_SINK]->set_level(spdlog::level::debug);
    levelsLogger->debug("文件也能看到Bug了");
}


//修改Pattern
void func7()
{
    spdlog::info("原有格式");
    spdlog::info("服务器已经在{}:{}开始监听","36.2.2.16",8080);
    spdlog::info("开始修改 Pattern");

    //先创建一个带颜色的控制台日志记录器
    auto mainLogger = spdlog::stdout_color_mt("主站");

    //修改它的Pattern
    mainLogger->set_pattern("[%Y年%m月%d日  %H:%M:%S]-%^ 【%l】 %n::%v%$");
   
    //创建一个文件 sink
    auto fileSink = std::make_shared<spdlog::sinks::basic_file_sink_mt>("log/pattern.txt");
    fileSink->set_pattern("[%Y-%m-%d %H:%M:%S] >%l< [%n] %v");
    mainLogger->sinks().push_back(fileSink);

    //调整为最低级别
    mainLogger->set_level(spdlog::level::trace);

    //取代默认
    spdlog::set_default_logger(mainLogger);

    spdlog::info("自定义格式起作用了");
    spdlog::info("服务器已经在{}:{}开始监听","36.2.2.16",8080);
    spdlog::warn("这是一个warn");
    spdlog::error("服务器无法链接数据库");
    spdlog::critical("严重错误");
    spdlog::trace("这是一个trace");
}

//flush缓冲区
void func8()
{
   auto fileSink = std::make_shared<spdlog::sinks::basic_file_sink_mt>("log/flush.txt");
   spdlog::default_logger()->sinks().push_back(fileSink);
   spdlog::info("写入日志，对比屏幕输出flush.txt内容");
 
   spdlog::default_logger()->flush();
   spdlog::info("已经强制刷新日志缓冲区");
}

//flush_every缓冲区
void func9()
{
    //每三秒强制清空一次
    spdlog::flush_every(std::chrono::seconds(3));
   auto fileSink = std::make_shared<spdlog::sinks::basic_file_sink_mt>("log/flush_every.txt");
   spdlog::default_logger()->sinks().push_back(fileSink);
   spdlog::info("写入日志，对比屏幕输出flush_every.txt内容，并等待3秒");
   spdlog::info("写入日志，对比屏幕输出flush_every.txt内容，并等待2秒");
   spdlog::info("写入日志，对比屏幕输出flush_every.txt内容，并等待1秒");

}


//异步日志记录器
void func10()
{
   spdlog::init_thread_pool(1000,1);
   //为异步日志记录器的工厂类型，取简短的别名
   //using async_factory = spdlog::async_factory_impl<spdlog::async_overflow_policy::block>;
   //using async_factory_nb = spdlog::async_factory_impl<spdlog::async_overflow_policy::overrun_oldest>;
   //创建带颜色的控制台日志记录器 使用指定异步工厂
   auto asyncColorLogger = spdlog::stdout_color_mt<spdlog::async_factory>("AsyncLogger");//async_factory_nonblock

   //创建文件
   auto fileSink = std::make_shared<spdlog::sinks::basic_file_sink_mt>("log/async_file.txt");
   asyncColorLogger->sinks().push_back(fileSink);

   spdlog::set_default_logger(asyncColorLogger);
   spdlog::info("异步记录器，它有{}个槽",spdlog::default_logger()->sinks().size());

}

int main()
{
    func10();

    return 0;
}