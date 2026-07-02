#include "multiThread/thread_pool.h"
#include <iostream>
#include <unistd.h>

class CountTask : public Task {
    int m_id;
    public:
    explicit CountTask(int id) : m_id(id) {}
    void execute() override {
        printf("    [Task %d] running in thread %lu\n", m_id, static_cast<unsigned long>(pthread_self()));
        usleep(10000);
    }
};

int main() {
    ThreadPool pool(10); // 10 个工作线程
    if (!pool.initialize()) {
        std::cerr << "ERROR: failed to initialize thread pool" << std::endl;
        return 1;
    }

    std::cout << "[Main] Submitting 100 tasks..." << std::endl;
    for (int i = 0; i < 100; i++) {
        pool.addTask(new CountTask(i));
    }

    sleep(2); // 更标准的做法应该是使用条件变量或计数器精确等待

    std::cout << "[Main] Shutting down..." << std::endl;
    pool.shutdown();
    std::cout << "[Main] Done." << std::endl;
    return 0;

}