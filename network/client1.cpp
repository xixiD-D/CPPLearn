#include <iostream>
#include <sys/socket.h>
#include <netdb.h>
#include <unistd.h>
#include <cstring>

int main(int argc, char *argv[])
{
    if (argc < 3)
    {
        std::cerr << "Usage: " << argv[0] << " <server_address>" << std::endl;
        return 1;
    }

    // 第 1 步：创建客户端的 socket
    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd == -1)
    {
        std::cerr << "Failed to create socket" << std::endl;
        return -1;
    }

    // 第 2 步：向服务器发送连接请求
    struct hostent *h; // 用于存放服务端的 IP 信息结构体
    if ((h = gethostbyname(argv[1])) == 0)
    {
        std::cerr << "Failed to get host by name" << std::endl;
        close(sockfd);
        return -1;
    }
    struct sockaddr_in serverAddr; // 用于存放服务器 IP 和端口的结构体
    memset(&serverAddr, 0, sizeof(serverAddr));
    serverAddr.sin_family = AF_INET;
    memcpy(&serverAddr.sin_addr, h->h_addr, h->h_length);
    serverAddr.sin_port = htons(atoi(argv[2])); // 服务器端口号
    // 向服务端发起连接
    if (connect(sockfd, (struct sockaddr *)&serverAddr, sizeof(serverAddr)) != 0)
    {
        std::cerr << "Failed to connect to server" << std::endl;
        close(sockfd);
        return -1;
    }

    // 第 3 步：与服务端通讯，客户端发送一个 请求 报文后等待服务端的回复，收到回复后，再发下一个报文
    char buffer[1024];
    // 循环 5 次，与服务端进行 5 次通讯
    for (int i = 0; i < 5; i++)
    {
        int iret;
        memset(buffer, 0, sizeof(buffer));
        sprintf(buffer, "Hello from client %d", i + 1);
        // 向 服务端发送 请求 报文
        if ((iret = send(sockfd, buffer, strlen(buffer), 0)) <= 0)
        {
            std::cerr << "Failed to send data to server" << std::endl;
            break;
        }
        std::cout << "Sent to server: " << buffer << std::endl;

        memset(buffer, 0, sizeof(buffer));

        // 接收服务端的 回复 报文，如果服务端没有发送回应报文，则 recv() 会阻塞，直到服务端发送回应报文
        if ((iret = recv(sockfd, buffer, sizeof(buffer), 0)) <= 0)
        {
            std::cerr << "Failed to receive data from server" << std::endl;
            break;
        }
        std::cout << "Received from server: " << buffer << std::endl;
        sleep(1); // 等待 1 秒后再发送下一个请求报文
    }

    // 第 4 步：关闭 socket 释放资源
    close(sockfd);
    return 0;
}