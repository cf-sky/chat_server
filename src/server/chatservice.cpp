#include "chatservice.hpp"
#include "public.hpp"

#include <muduo/base/Logging.h>
#include <string>
#include <vector>
#include <iostream>
using namespace std;
using namespace muduo;

//获取单例对象的接口函数
ChatService *ChatService::instance()
{
    static ChatService service;
    return &service;
}

//注册消息以及对应的回调操作
ChatService::ChatService()
{
    _msgHandlerMap.insert({LOGIN_MSG, std::bind(&ChatService::login, this, _1, _2, _3)});
    _msgHandlerMap.insert({REG_MSG, std::bind(&ChatService::reg, this, _1, _2, _3)});
    _msgHandlerMap.insert({ONE_CHAT_MSG, std::bind(&ChatService::oneChat, this, _1, _2, _3)});
    _msgHandlerMap.insert({ADD_FRIEND_MSG, std::bind(&ChatService::addFriend, this, _1, _2, _3)});
    _msgHandlerMap.insert({CREATE_GROUP_MSG, std::bind(&ChatService::createGroup, this, _1, _2, _3)});
    _msgHandlerMap.insert({ADD_GROUP_MSG, std::bind(&ChatService::addGroup, this, _1, _2, _3)});
    _msgHandlerMap.insert({GROUP_CHAT_MSG, std::bind(&ChatService::groupChat, this, _1, _2, _3)});
    _msgHandlerMap.insert({LOGINOUT_MSG, std::bind(&ChatService::loginout, this, _1, _2, _3)});

    //连接redis服务器
    if (_redis.connect())
    {
        //设置上报消息的回调
        _redis.init_notify_handler(std::bind(&ChatService::handleRedisSubscribeMessage, this, _1, _2));
    }
}

//获取消息对应的处理器
MsgHandler ChatService::getHandler(int msgid)
{
    //记录错误日志，msgid没有对应的事件处理回调
    auto it = _msgHandlerMap.find(msgid);
    if (it == _msgHandlerMap.end())
    {
        //返回一个默认的处理器，空操作
        return [=](const TcpConnectionPtr &conn, json &js, Timestamp)
        {
            LOG_ERROR << "msgid:" << msgid << " can not find handler!";
        };
    }
    else
    {
        return _msgHandlerMap[msgid];
    }
}

//处理登录业务
void ChatService::login(const TcpConnectionPtr &conn, json &js, Timestamp)
{
    int id = js["id"];
    string pwd = js["password"];

    User user = _userModel.query(id);
    if (user.getId() == id && user.getPwd() == pwd)
    {
        //该用户已经登录，不允许重复登录
        if (user.getState() == "online")
        {
            json response;
            response["msgid"] = LOGIN_MSG_ACK;
            response["errno"] = 2;
            response["errmsg"] = "this count is using, input another!";
            conn->send(response.dump());
        }
        //登陆成功，更新用户状态信息state offline->online
        else
        {
            //登陆成功，记录用户连接信息
            {
                lock_guard<mutex> lock(_connMutex);
                _userConnMap.insert({id, conn});
            }

            // id用户登录成功后，向redis订阅channel(id)
            _redis.subscribe(id);

            user.setState("online");
            _userModel.updateState(user);

            json response;
            response["msgid"] = LOGIN_MSG_ACK;
            response["errno"] = 0;
            response["id"] = user.getId();
            response["name"] = user.getName();
            //查询用户是否有离线消息
            vector<string> vec = _offlineMsgModel.query(id);
            if (!vec.empty())
            {
                response["offlinemsg"] = vec;
                //读取用户的离线消息后，把该用户的所有离线消息删除掉
                _offlineMsgModel.remove(id);
            }
            //查询该用户的好友信息并返回
            vector<User> userVec = _friendModel.query(id);
            if (!userVec.empty())
            {
                vector<string> vec2;
                for (User &user : userVec)
                {
                    json js;
                    js["id"] = user.getId();
                    js["name"] = user.getName();
                    js["state"] = user.getState();
                    vec2.push_back(js.dump());
                }

                response["friends"] = vec2;
            }

            // 获取当前用户id所在群组信息
            vector<Group> groupUserVec = _groupModel.queryGroups(id);
            if (!groupUserVec.empty())
            {
                vector<string> groupVec;
                // 开始打包群组信息
                for (Group &group : groupUserVec)
                {
                    // 一个groupJson包括群id、群名和描述信息，以及所有的成员信息
                    json groupJson;
                    groupJson["groupid"] = group.getId();
                    groupJson["groupname"] = group.getName();
                    groupJson["groupdesc"] = group.getDesc();

                    // userVec每个元素就是一个组员的信息，一个userVec就是一个群所有的成员的信息
                    vector<string> userVec;
                    for (GroupUser &guser : group.getUsers())
                    {
                        // 一个guserJson封装一个群成员的信息
                        json guserJson;
                        guserJson["id"] = guser.getId();
                        guserJson["name"] = guser.getName();
                        guserJson["state"] = guser.getState();
                        guserJson["role"] = guser.getRole();
                        userVec.push_back(guserJson.dump());
                    }

                    groupJson["users"] = userVec;
                    groupVec.push_back(groupJson.dump());
                }

                response["groups"] = groupVec;
            }

            conn->send(response.dump());
        }
    }
    else
    {
        //该用户不存在或者用户存在但是密码错误，登陆失败
        json responce;
        responce["msgid"] = LOGIN_MSG_ACK;
        responce["errno"] = 1;
        responce["errmsg"] = "id or password is invalid!";
        conn->send(responce.dump());
    }
}
//处理注册业务
void ChatService ::reg(const TcpConnectionPtr &conn, json &js, Timestamp)
{
    string name = js["name"];
    string pwd = js["password"];

    User user;
    user.setName(name);
    user.setPwd(pwd);
    bool state = _userModel.insert(user);

    if (state)
    {
        //注册成功
        json responce;
        responce["msgid"] = REG_MSG_ACK;
        // errno=0响应成功，不为0那肯定还有errmsg说明错误消息
        responce["errno"] = 0;
        responce["id"] = user.getId();
        conn->send(responce.dump());
    }
    else
    {
        //注册失败
        json responce;
        responce["msgid"] = REG_MSG_ACK;
        responce["errno"] = 1;
        conn->send(responce.dump());
    }
}

//一对一聊天业务
void ChatService::oneChat(const TcpConnectionPtr &conn, json &js, Timestamp)
{
    int toid = js["toid"].get<int>();

    {
        lock_guard<mutex> lock(_connMutex);
        auto it = _userConnMap.find(toid);
        if (it != _userConnMap.end())
        {
            // toid在线，转发消息，服务器主动推送消息给toid用户
            it->second->send(js.dump());
            return;
        }
    }

    //查询toid是否在线
    User user = _userModel.query(toid);
    if (user.getState() == "online")
    {
        _redis.publish(toid, js.dump());
        return;
    }

    // 接收者不在线，需要存储离线消息
    _offlineMsgModel.insert(toid, js.dump());
}

//添加好友业务
void ChatService::addFriend(const TcpConnectionPtr &conn, json &js, Timestamp)
{
    int userid = js["id"].get<int>();
    int friendid = js["friendid"].get<int>();

    //存储好友信息
    _friendModel.insert(userid, friendid);
}
//创建群组业务
void ChatService::createGroup(const TcpConnectionPtr &conn, json &js, Timestamp)
{
    int userid = js["id"].get<int>();
    string name = js["groupname"];
    string desc = js["groupdesc"];

    // 存储新创建的群组信息，群id初始化为-1
    Group group(-1, name, desc);
    // 访问数据库的allgroup表，插入群组信息
    if (_groupModel.createGroup(group))
    {
        // 群名重复时，则创建失败
        // 创建成功，则访问groupuser表，将当前userid设置为creator
        _groupModel.addGroup(userid, group.getId(), "creator");
    }
    //如果要给客户端响应，用conn给客户端发json就可
}
//加入群组业务
void ChatService::addGroup(const TcpConnectionPtr &conn, json &js, Timestamp)
{
    int userid = js["id"].get<int>();
    int groupid = js["groupid"].get<int>();
    // 数据库层访问groupuser，一个用户加入群，添加一条记录
    _groupModel.addGroup(userid, groupid, "normal");
}
//群组聊天业务
void ChatService::groupChat(const TcpConnectionPtr &conn, json &js, Timestamp)
{
    int userid = js["id"].get<int>();
    int groupid = js["groupid"].get<int>();

    // 查询groupid中除了userid以外的其他成员
    vector<int> useridVec = _groupModel.queryGroupUsers(userid, groupid);

    //读写_userConnMap需要保证线程安全
    lock_guard<mutex> lock(_connMutex);
    // 遍历所有的组员，若在线则转发消息，不在线则保存至离线消息表
    for (int id : useridVec)
    {
        auto it = _userConnMap.find(id);
        if (it != _userConnMap.end())
        {
            //找出连接，直接转发json
            it->second->send(js.dump());
        }
        else
        {
            //查询toid是否在线
            User user = _userModel.query(id);
            if (user.getState() == "online")
            {
                _redis.publish(id, js.dump());
                return;
            }
            else
            {
                _offlineMsgModel.insert(id, js.dump());
            }

        }
    }
}

//处理注销业务
void ChatService::loginout(const TcpConnectionPtr &conn, json &js, Timestamp)
{
    int userid = js["id"].get<int>();
    {
        lock_guard<mutex> lock(_connMutex);
        auto it = _userConnMap.find(userid);
        if (it == _userConnMap.end())
        {
            _userConnMap.erase(it);
        }
    }

    //用户注销，相当于是下线，在redis中取消订阅通道
    _redis.unsubscribe(userid);

    //更新用户的状态信息
    User user(userid, "", "", "offline");
    _userModel.updateState(user);
}

//处理客户端异常退出
void ChatService::clientCloseException(const TcpConnectionPtr &conn)
{
    User user;
    {
        lock_guard<mutex> lock(_connMutex);
        for (auto it = _userConnMap.begin(); it != _userConnMap.end(); ++it)
        {
            if (it->second == conn)
            {
                //从map表删除用户的连接信息
                user.setId(it->first);
                _userConnMap.erase(it);
                break;
            }
        }
    }

    //用户注销，相当于是下线，在redis中取消订阅通道
    _redis.unsubscribe(user.getId());

    //更新用户的状态信息
    if (user.getId() != -1)
    {
        user.setState("offline");
        _userModel.updateState(user);
    }
}

//服务器异常，业务重置方法
void ChatService::reset()
{
    //把online状态的用户设置成offline
    _userModel.resetState();
}

//从redis消息队列中获取订阅消息
void ChatService::handleRedisSubscribeMessage(int userid, string msg)
{
    lock_guard<mutex> lock(_connMutex);
    // 因为是其他服务器发现toid在线，但是不在那台服务器上，才会将消息publish到消息队列
    // 然后当前服务器才能执行handleRedisSubscribeMessage拿到消息，一般情况下，toid都是在线的
    auto it = _userConnMap.find(userid);
    if(it != _userConnMap.end())
    {
        it->second->send(msg);
        return;
    }
    //存储该用户的离线消息
    // 也许就是其他服务器publish后，当前服务器拿到msg前，toid下线了
    // 才可能需要存储该用户的离线消息
    _offlineMsgModel.insert(userid, msg);
}


