
#pragma once

#include <vector>
#include <mutex>

class QueryCallback;
class AsyncProcessor
{
public:
    void AddQueryCallback(QueryCallback &&query_callback);
    void InvokeIfReady();

private:
    std::vector<QueryCallback> pending_queries_;
};

