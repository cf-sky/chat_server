#include "usermodel.hpp"
#include "db.h"

#include <iostream>
using namespace std;

bool UserModel::insert(User &user)
{
    // 1. 组装sql语句
    char sql[1024] = {0};
    // 此处刚注册，还没有登录，肯定是offline，我们在User类的构造函数里state默认为offline
    sprintf(sql, "insert into user(name, password, state) values('%s', '%s', '%s')",
            user.getName().c_str(), user.getPwd().c_str(), user.getState().c_str());
    MySQL mysql;

    if (mysql.connect()) // 2. 连接数据库
    {
        if (mysql.update(sql)) // 3. 发送sql语句插入数据
        {
            //获取插入成功的用户数据生成的主键id
            user.setId(mysql_insert_id(mysql.getConnection()));
            return true;
        }
    }
    return false;
}
User UserModel::query(int id)
{
    // 1. 组装sql语句
    char sql[1024] = {0};
    // 此处刚注册，还没有登录，肯定是offline，我们在User类的构造函数里state默认为offline
    sprintf(sql, "select * from user where id = %d", id);
    MySQL mysql;

    if (mysql.connect()) // 2. 连接数据库
    {
        // malloc申请了资源，需要释放
        MYSQL_RES *res = mysql.query(sql);
        if (res != nullptr)
        {
            MYSQL_ROW row = mysql_fetch_row(res);
            if (row != nullptr)
            {
                User user;
                user.setId(atoi(row[0]));
                user.setName(row[1]);
                user.setPwd(row[2]);
                user.setState(row[3]);
                // 释放结果集占用的资源
                mysql_free_result(res);
                return user;
            }
        }
    }
    // 如果数据库没有连接上，或者没查找到id对应数据，则返回默认构造的user，id=-1
    return User();
}
//更新用户的状态信息
bool UserModel::updateState(User user)
{
    // 1. 组装sql语句
    char sql[1024] = {0};
    sprintf(sql, "update user set state = '%s' where id = %d", user.getState().c_str(), user.getId());
    MySQL mysql;

    if (mysql.connect()) // 2. 连接数据库
    {
        if (mysql.update(sql))
        {
            return true;
        }
    }
    return false;
}
//重置用户的状态信息
void UserModel::resetState()
{
    // 1. 组装sql语句
    char sql[1024] = "update user set state = 'offline' where state =  'online'";
    MySQL mysql;

    if (mysql.connect()) // 2. 连接数据库
    {
        mysql.update(sql);
    }
}