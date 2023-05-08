#include<muduo/net/TcpServer.h>
#include<muduo/net/EventLoop.h>
#include<iostream>
#include<functional>
#include<string>
using namespace std;
using namespace muduo;
using namespace muduo::net;
using namespace placeholders;

/*基于muduo网络库开发服务器程序
1. 组合TcpServer对象
2. 创建EventLoop事件循环对象的指针，可以看做epoll，可以向loop上注册感兴趣的事件，如果有事件发生了，loop会上报
3. 明确TcpServer构造函数需要什么参数，输出ChatServer的构造函数
4. 在当前服务器类的构造函数当中，注册处理连接的回调函数和处理读写时间的回调函数；当我们知道一个函数什么时候发生我们就可以直接调用该函数，如果我们不知道什么时候发生，就需要使用回调机制，先用函数对象写好，等条件合适时通过函数对象调用
5. 设置合适的服务端线程数量，muduo库会自己分配I/O线程和worker线程
*/
class ChatServer
{
public:
    //TcpServer没有默认的构造函数，所以这里需要指定相应的构造
    ChatServer(EventLoop* loop, //事件循环
            const InetAddress& listenAddr,  //ip+port
            const string& nameArg)  //服务器名字
            :_server(loop,listenAddr,nameArg)
            ,_loop(loop)
            {
                //给服务器注册用户连接的创建和断开回调,回调就是对端的相应事件发生了告诉网络库 ，然后网络库告诉我 ，我在回调函数开发业务
                //人家这个setConnectionCallback是没有返回值以及有一个形参变量，而我们现在写的是一个成员方法，写成成员方法是因为想访问对象的成员变量，
                //写成员方法的话就有一个this指针，和人家setConnectionCallback的类型就不同了，所以在这里我们用绑定器绑定
                _server.setConnectionCallback(std::bind(&ChatServer::onConnection,this,_1));
                //给服务器注册用户读写事件回调
                _server.setMessageCallback(std::bind(&ChatServer::onMessage,this,_1,_2,_3));
                //设置服务器端的线程数量    1个io线程，3个worker线程
                _server.setThreadNum(4);
            }
            //开始事件循环
            void start()
            {
                _server.start();
            }
private:
    //专门处理用户的连接创建和断开
    /*
    我们自己在编写epoll，从epoll拿过来事件以后，发现它如果是listenfd，会从listenfd上去accept，这就表示有新用户连接了，
    拿出来一个和该用户专门通信的socket,这一切底层muduo库都封装了，只暴露了一个回调的接口，只管写就行了，不用关心什么时候会调用，
    我们都把这个注册到muduo库上了，当有新用户连接的创建以及原来用户连接的断开，这个方法就会响应
    */
    void onConnection(const TcpConnectionPtr &conn)
    {
        if(conn->connected())
        {
            cout<<conn->peerAddress().toIpPort()<<"->"<<conn->localAddress().toIpPort()<<"state:online"<<endl;
        }
        else
        {
            cout<<conn->peerAddress().toIpPort()<<"->"<<conn->localAddress().toIpPort()<<"state:offline"<<endl;
            conn->shutdown();     //close(fd)
            //_loop->quit();//服务器就结束了
        }
    }
    //专门处理用户的读写事件
    void onMessage(const TcpConnectionPtr &conn,    // 连接
                            Buffer *buffer,    //缓冲区
                            Timestamp time) //接收到数据的时间信息
    {
        string buf = buffer->retrieveAllAsString();
        cout<<"recv data:"<<buf<<"time:"<<time.toString()<<endl;
        conn->send(buf);
    }
    TcpServer _server;  //#1
    EventLoop *_loop;   //#2
};
int main()
{
    EventLoop loop; //epoll
    InetAddress addr("127.0.0.1",6000);
    ChatServer server(&loop,addr,"ChatServer");
    server.start(); //启动服务，把listenfd通过epoll_ctl添加到epoll上
    loop.loop();    //epoll_wait以阻塞的方式等待新用户的链接，已连接用户的读写事件等
    return 0;
}

