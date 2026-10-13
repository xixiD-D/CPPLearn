
#include "SQLOperation.h"

#include "MySQLConn.h"

void SQLOperation::Execute(MySQLConn *conn)
{
    auto result = conn->Query(sql_);
    promise_.set_value(std::unique_ptr<sql::ResultSet>(result));
}
