#include "chatserver.hpp"
#include "json.hpp"
#include "chatservice.hpp"

#include<iostream>
#include <functional>
#include<string>
using namespace std;
using namespace placeholders;
using json = nlohmann :: json;

ChatServer ::ChatServer(EventLoop *loop,
                        const InetAddress &listenAddr,
                        const string &nameArg)
    : _server(loop, listenAddr, nameArg), _loop(loop)
{
    //注册连接回调
    _server.setConnectionCallback(std::bind(&ChatServer::onConnection, this, _1));

    //注册消息回调
    _server.setMessageCallback(std::bind(&ChatServer::onMessage, this, _1, _2, _3));

    //设置线程数量
    _server.setThreadNum(4);
}
void ChatServer::start()
{
    _server.start();
}

void ChatServer::onConnection(const TcpConnectionPtr &conn)
{
    if(!conn->connected()){
        ChatService::instance()->clientCloseException(conn);
        conn->shutdown();  // 关闭套接字的写端
        // loop.quit();
    }
}

void ChatServer::onMessage(const TcpConnectionPtr &conn,
                           Buffer *buffer,
                           Timestamp time)
{
    string buf = buffer->retrieveAllAsString();

    //数据的反序列化
    json js = json::parse(buf);
    //json里肯定包含msg_type，要根据什么业务调用什么方法这样不好，会把网络模块的代码和业务模块代码强耦合到一块
    //通过js["msgid"]，实现绑定一个回调操作，即一个id对应一个操作，这样就不用管网络具体做什么业务，
    // 只解析到msgid就可以通过msgid获取到一个业务处理器handler,这个处理器就是事先绑定的一个方法，在网络模块是看不见的
    //达到的目的：利用oop上回调的思想完全解耦网络模块和业务模块的代码
    /*在oop语言与要解耦模块之间的关系有两种
    1. 使用基于面向接口的编程，在C++里接口就是抽象基类
    2. 基于回调函数
    */
   auto msgHandler = ChatService::instance()->getHandler(js["msgid"].get<int>());
   //回调消息对应的绑定好的事件处理器来执行相应的业务处理
   msgHandler(conn,js,time);

}