#include "MySQLWorker.h"

#include "BlockingQueue.h"
#include "SQLOperation.h"
#include "MySQLConn.h"

MySQLWorker::MySQLWorker(MySQLConn *conn,
                         BlockingQueue<std::unique_ptr<SQLOperation>> &task_queue,
                         int heartbeat_interval_ms)
    : conn_(conn), task_queue_(task_queue), heartbeat_interval_ms_(heartbeat_interval_ms)
{
}

MySQLWorker::~MySQLWorker()
{
    RequestStop();
    Stop();
}

void MySQLWorker::Start()
{
    worker_ = std::thread(&MySQLWorker::Worker, this);
}

void MySQLWorker::Stop()
{
    if (worker_.joinable())
    {
        worker_.join();
    }
}

void MySQLWorker::RequestStop()
{
    should_stop_ = true;
}

void MySQLWorker::Worker()
{
    while (!should_stop_)
    {
        std::unique_ptr<SQLOperation> op;
        if (!task_queue_.PopWithTimeout(op, heartbeat_interval_ms_))
        {
            if (should_stop_)
                break;
            conn_->Ping();
            continue;
        }

        if (op)
        {
            op->Execute(conn_);
        }
    }
}
