#include "AsyncProcessor.h"
#include "QueryCallback.h"

void AsyncProcessor::AddQueryCallback(QueryCallback &&query_callback)
{
    pending_queries_.emplace_back(std::move(query_callback));
}

void AsyncProcessor::InvokeIfReady()
{
    for (auto it = pending_queries_.begin(); it != pending_queries_.end();)
    {
        if (it->InvokeIfReady())
            it = pending_queries_.erase(it);
        else
            ++it;
    }
}