/*
 * 程序名：demo8.cpp，此程序用于演示封装socket通讯的服务端
 */
#include <iostream>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <unistd.h>
#include <netdb.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>

class ctcpserver // TCP通讯的服务端类。
{
private:
    int m_listenfd;         // 监听的socket，-1表示未初始化。
    int m_clientfd;         // 客户端连上来的socket，-1表示客户端未连接。
    std::string m_clientip; // 客户端字符串格式的IP。
    unsigned short m_port;  // 服务端用于通讯的端口。
public:
    ctcpserver() : m_listenfd(-1), m_clientfd(-1) {}

    // 初始化服务端用于监听的socket。
    bool initserver(const unsigned short in_port)
    {
        // 第1步：创建服务端的socket。
        if ((m_listenfd = socket(AF_INET, SOCK_STREAM, 0)) == -1)
        {
            return false;
        }

        m_port = in_port;

        // 第2步：把服务端用于通信的IP和端口绑定到socket上。
        struct sockaddr_in servaddr; // 用于存放协议、端口和IP地址的结构体。
        memset(&servaddr, 0, sizeof(servaddr));
        servaddr.sin_family = AF_INET;                // ①协议族，固定填AF_INET。
        servaddr.sin_port = htons(m_port);            // ②指定服务端的通信端口。
        servaddr.sin_addr.s_addr = htonl(INADDR_ANY); // ③如果操作系统有多个IP，全部的IP都可以用于通讯。

        // 绑定服务端的IP和端口（为socket分配IP和端口）。
        if (bind(m_listenfd, (struct sockaddr *)&servaddr, sizeof(servaddr)) == -1)
        {
            close(m_listenfd);
            m_listenfd = -1;
            return false;
        }

        // 第3步：把socket设置为可连接（监听）的状态。
        if (listen(m_listenfd, 5) == -1)
        {
            close(m_listenfd);
            m_listenfd = -1;
            return false;
        }

        return true;
    }

    // 受理客户端的连接（从已连接的客户端中取出一个客户端），
    // 如果没有已连接的客户端，accept()函数将阻塞等待。
    bool accept()
    {
        struct sockaddr_in caddr;          // 客户端的地址信息。
        socklen_t addrlen = sizeof(caddr); // struct sockaddr_in的大小。
        if ((m_clientfd = ::accept(m_listenfd, (struct sockaddr *)&caddr, &addrlen)) == -1)
        {
            return false;
        }

        m_clientip = inet_ntoa(caddr.sin_addr); // 把客户端的地址从大端序转换成字符串。

        return true;
    }

    // 获取客户端的IP(字符串格式)。
    const std::string &clientip() const
    {
        return m_clientip;
    }

    // 向对端发送报文，成功返回true，失败返回false。
    bool send(const std::string &buffer)
    {
        if (m_clientfd == -1)
        {
            return false;
        }

        if ((::send(m_clientfd, buffer.data(), buffer.size(), 0)) <= 0)
        {
            return false;
        }

        return true;
    }

    // 接收对端的报文，成功返回true，失败返回false。
    // buffer-存放接收到的报文的内容，maxlen-本次接收报文的最大长度。
    bool recv(std::string &buffer, const size_t maxlen)
    {
        buffer.clear();                                               // 清空容器。
        buffer.resize(maxlen);                                        // 设置容器的大小为maxlen。
        int readn = ::recv(m_clientfd, &buffer[0], buffer.size(), 0); // 直接操作buffer的内存。
        if (readn <= 0)
        {
            buffer.clear();
            return false;
        }
        buffer.resize(readn); // 重置buffer的实际大小。

        return true;
    }

    // 关闭监听的socket。
    bool closelisten()
    {
        if (m_listenfd == -1)
            return false;

        ::close(m_listenfd);
        m_listenfd = -1;
        return true;
    }

    // 关闭客户端连上来的socket。
    bool closeclient()
    {
        if (m_clientfd == -1)
            return false;

        ::close(m_clientfd);
        m_clientfd = -1;
        return true;
    }

    ~ctcpserver()
    {
        closelisten();
        closeclient();
    }
}