#include <iostream>
#include <sys/socket.h>

int main(int argc, char *argv[])
{
    if (argc < 2)
    {
        std::cerr << "Usage: " << argv[0] << " <port>" << std::endl;
        return -1;
    }

    // 第 1 步：创建服务端的 socket
    int listenfd = socket(AF_INET, SOCK_STREAM, 0);
    if (listenfd == -1)
    {

        std::cerr << "Failed to create socket" << std::endl;
        return -1;
    }

    // 第 2 步：绑定 socket 与端口号
    struct sockaddr_in serverAddr; // 用于存放服务器 IP 和端口的结构体
    memset(&serverAddr, 0, sizeof(serverAddr));
    serverAddr.sin_family = AF_INET;                // 指定协议
    serverAddr.sin_addr.s_addr = htonl(INADDR_ANY); // 服务器端任意网卡 IP 都可以连接
    serverAddr.sin_port = htons(atoi(argv[1]));     // 服务器端口号
    if (bind(listenfd, (struct sockaddr *)&serverAddr, sizeof(serverAddr)) != 0)
    {
        std::cerr << "Failed to bind socket" << std::endl;
        close(listenfd);
        return -1;
    }

    // 第 3 步：把 socket 设置为监听模式，等待客户端的连接请求
    if (listen(listenfd, 5) != 0)
    {
        std::cerr << "Failed to listen on socket" << std::endl;
        close(listenfd);
        return -1;
    }

    // 第 4 步：等待客户端的连接请求，accept() 会阻塞直到有客户端连接
    int clientfd = accept(listenfd, nullptr, nullptr);
    if (clientfd == -1)
    {
        std::cerr << "Failed to accept client connection" << std::endl;
        close(listenfd);
        return -1;
    }

    std::cout << "Client connected" << std::endl;

    // 第 5 步：与客户端通讯，服务端接收客户端的请求报文后，回复一个响应报文
    char buffer[1024];
    while (true)
    {
        int iret;
        memset(buffer, 0, sizeof(buffer));
        // 接收客户端的请求报文
        if ((iret = recv(clientfd, buffer, sizeof(buffer), 0)) <= 0)
        {
            std::cerr << "Failed to receive data from client" << std::endl;
            break;
        }
        std::cout << "Received from client: " << buffer << std::endl;

        //  生成报文回应内容
        strcpy(buffer, "ok");
        if ((irev = send(clientfd,  buffer, strlen(buffer), 0)) <= 0)
        {
            std::cerr << "Failed to send data to client" << std::endl;
        } else {
            
            std::cout << "Sent to client: " << buffer << std::endl;
        }
    }

    // 第 6 步：关闭 socket 释放资源
    close(clientfd);
    close(listenfd);

    return 0;
}