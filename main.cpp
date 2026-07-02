#include <iostream>
#include <sys/epoll.h>
#include <unistd.h>

int main() {

    int epfd = epoll_create1(EPOLL_CLOEXEC);

    std::cout << "Hello Linux C++! epoll fd=" << epfd << std::endl;

    close(epfd);

    return 0;

}
