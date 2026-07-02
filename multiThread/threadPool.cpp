//
// Created by zexi zhong on 2026/7/1.
//
struct nTask {
    void (*task_func)(void *arg);
    void *user_data;

    struct nTask *prev;
    struct nTask *next;
};

struct nWorker {
    pthread_t threadId;

    struct nWorker *prev;
    struct nWorker *next;
};

struct nManager {
    struct nTask *tasks;
    struct nWorker *workers;

    pthread_mutex_t mutex;
    pthread_cond_t cond;
};