#include "MySQLConn.h"
#include "MySQLWorker.h"
#include "BlockingQueue.h"

#include <cppconn/driver.h>
#include <cppconn/connection.h>
#include <cppconn/exception.h>
#include <cppconn/statement.h>
#include <cppconn/resultset.h>

#include <iostream>
#include <memory>

MySQLConn::MySQLConn(const MySQLConfig &config, BlockingQueue<std::unique_ptr<SQLOperation>> &task_queue)
    : config_(config)
{
    worker_ = std::make_unique<MySQLWorker>(this, task_queue, config_.heartbeat_interval_s * 1000);
}

MySQLConn::~MySQLConn()
{
    if (worker_)
    {
        worker_->RequestStop();
        worker_->Stop();
    }
    Close();
}

int MySQLConn::Open()
{
    int err = 0;
    try
    {
        driver_ = get_driver_instance();
        conn_.reset(driver_->connect(config_.GetUrl(), config_.user, config_.password));
        if (!conn_)
        {
            return -1;
        }
        conn_->setSchema(config_.database);
    }
    catch (sql::SQLException &e)
    {
        HandlerException(e);
        err = e.getErrorCode();
    }
    return err;
}

void MySQLConn::Close()
{
    if (conn_)
    {
        try
        {
            conn_->close();
        }
        catch (...)
        {
        }
        conn_.reset();
    }
}

void MySQLConn::StartWorker()
{
    if (worker_)
    {
        worker_->Start();
    }
}

bool MySQLConn::Ping()
{
    try
    {
        if (conn_ && !conn_->isClosed())
        {
            std::unique_ptr<sql::Statement> stmt(conn_->createStatement());
            std::unique_ptr<sql::ResultSet> res(stmt->executeQuery("SELECT 1"));
            return true;
        }
    }
    catch (sql::SQLException &e)
    {
        HandlerException(e);
    }
    return Reconnect();
}

bool MySQLConn::Reconnect()
{
    Close();
    return Open() == 0;
}

sql::ResultSet *MySQLConn::Query(const std::string &sql)
{
    try
    {
        std::unique_ptr<sql::Statement> stmt(conn_->createStatement());
        return stmt->executeQuery(sql);
    }
    catch (sql::SQLException &e)
    {
        HandlerException(e);
    }
    return nullptr;
}

void MySQLConn::RequestStop()
{
    if (worker_)
    {
        worker_->RequestStop();
    }
}

void MySQLConn::HandlerException(sql::SQLException &e)
{
    if (e.getErrorCode() != 0)
    {
        std::cerr << "# ERR: SQLException in " << __FILE__;
        std::cerr << "(" << __FUNCTION__ << ") on line " << __LINE__ << std::endl;
        std::cerr << "# ERR: " << e.what();
        std::cerr << " (MySQL error code: " << e.getErrorCode();
        std::cerr << ", SQLState: " << e.getSQLState() << " )" << std::endl;
    }
}
