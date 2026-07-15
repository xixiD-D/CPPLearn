# C++ 并发编程与数据库连接池学习笔记

> 本笔记基于从零构建线程池、MySQL 连接池、以及二者结合架构的完整学习过程，涵盖 C++98 底层实现与现代 C++（C++11/14/17）对比，同时包含并发原语与数据库二进制存储的相关概念。

---

## 一、线程池（Thread Pool）

### 1.1 核心概念

线程池是**对象池模式**在并发领域的应用，核心思想为三个词：**预分配、复用、统一管理**。

如果采用"来一个请求创建一个线程"的策略，每次都要经历：用户态栈分配 → 内核 PCB 创建 → 调度器加入队列 → 任务结束后资源回收。这个开销在高并发下会压垮系统。

线程池的解决思路：**程序启动时预先创建一组线程，让它们阻塞等待；有任务来时唤醒一个线程执行，执行完后不销毁，继续等待下一个任务。**

### 1.2 线程池 = 生产者-消费者模型

| 角色 | 对应组件 | 职责 |
|------|---------|------|
| 生产者 | 调用 `addTask()` / `submit()` 的线程 | 生成任务，放入队列 |
| 缓冲区 | 任务队列 `Task Queue` | 解耦生产与消费速率 |
| 消费者 | 工作线程 `Worker Threads` | 从队列取任务并执行 |
| 同步机制 | 互斥锁 + 条件变量 | 保护队列 + 阻塞/唤醒线程 |

### 1.3 为什么必须用条件变量（Condition Variable）？

没有条件变量时，工作线程只能**忙等待**（Busy Loop）：

```cpp
while (queue.empty()) {
    usleep(1000);  // 空转，浪费 CPU
}
```

条件变量提供**事件驱动**的阻塞机制：工作线程发现没任务时，调用 `wait()` 进入内核等待队列，**完全不占用 CPU**；生产者放入任务后调用 `notify_one()` / `notify_all()` 唤醒线程。

### 1.4 虚假唤醒（Spurious Wakeup）

`wait()` 可能无故返回，因此必须用 `while` 循环检查条件，不能用 `if`：

```cpp
while (queue.empty() && !stop) {  // 必须用 while
    condition.wait(lock);
}
```

### 1.5 优雅退出的四个步骤

1. 设置停止标志，告诉线程"不要再等了"
2. 唤醒所有正在 `wait` 的线程（`notify_all` / `broadcast`）
3. 等待所有线程真正结束（`join`），防止资源泄漏
4. 清理队列中未执行的任务，销毁锁和条件变量

### 1.6 C++98 vs 现代 C++ 线程池对比

| 维度 | C++98 (pthread) | 现代 C++ |
|------|----------------|----------|
| 线程句柄 | `pthread_t` | `std::thread` |
| 互斥锁 | `pthread_mutex_t` + 手动 lock/unlock | `std::mutex` + `std::unique_lock` |
| 条件变量 | `pthread_cond_wait` + 绝对时间计算 | `std::condition_variable::wait` + `std::chrono` |
| 任务抽象 | 虚基类 `Task` + `execute()` | `std::function<void()>` + lambda |
| 任务参数 | 子类成员变量硬编码 | 可变参数模板 + `std::bind` + 完美转发 |
| 返回值 | 无（或回调函数） | `std::future<T>` 异步获取结果 |
| 资源管理 | 手动 `new/delete` | `std::unique_ptr` / `std::shared_ptr` + RAII |
| 停止标志 | `bool`（需锁保护） | `std::atomic<bool>`（无锁原子操作） |
| 线程 join | 手动 `pthread_join` | `thread.join()` + `thread.joinable()` |

### 1.7 核心代码：现代 C++ 任务提交

```cpp
template<typename F, typename... Args>
auto submit(F&& f, Args&&... args) -> std::future<decltype(f(args...))> {
    using ReturnType = decltype(f(args...));
    auto task = std::make_shared<std::packaged_task<ReturnType()>>(
        std::bind(std::forward<F>(f), std::forward<Args>(args)...)
    );
    std::future<ReturnType> result = task->get_future();
    {
        std::unique_lock<std::mutex> lock(m_queueMutex);
        m_tasks.emplace([task]() { (*task)(); });
    }
    m_condition.notify_one();
    return result;
}
```

**关键点：**
- `std::packaged_task` 将可调用对象与 `promise` 绑定，执行结果自动写入 `future`
- `std::function` 提供**类型擦除**，任意可调用对象（lambda、函数指针、成员函数）都能存入队列
- `std::forward` 保持参数的**值类别**，右值移动、左值拷贝

---

## 二、MySQL 连接池（Connection Pool）

### 2.1 为什么需要连接池？

一次 `mysql_real_connect()` 的完整开销：
1. TCP 三次握手（如果走网络）
2. MySQL 协议握手：服务端发送握手包 → 客户端认证 → 服务端验证权限 → 发送 OK 包
3. 会话状态初始化：分配线程句柄、初始化会话变量、解析权限表
4. 内存分配：服务端为每个连接分配资源

如果"用一次建一次"，高并发下这个开销会压垮数据库。**连接池核心思想：预建连接、复用连接、统一管理。**

### 2.2 连接池 vs 线程池

| 维度 | 线程池 | 连接池 |
|------|--------|--------|
| 管理对象 | 操作系统线程 | 数据库网络连接 |
| 核心问题 | CPU 调度、并发控制 | 网络资源、认证开销、数据库负载 |
| 阻塞点 | 任务队列空/满 | 连接数达到上限、连接断开 |
| 回收机制 | 线程长期存活 | 连接空闲超时关闭，或定期保活 |

两者**正交**：线程池解决"谁来干活"，连接池解决"用什么资源干活"。生产环境通常两者配合使用。

### 2.3 连接池必须解决的五个问题

1. **线程安全**：多个线程并发 `getConnection()`，必须锁保护空闲队列
2. **连接上限**：数据库有 `max_connections` 限制，超出时线程等待而非无限创建
3. **超时等待**：获取连接不能无限阻塞，超时返回错误
4. **健康检查**：连接可能因网络抖动、数据库重启、`wait_timeout` 超时而静默关闭，复用前必须检测
5. **优雅退出**：关闭时归还所有连接并调用 `mysql_close()`，防止泄漏

### 2.4 健康检查：`mysql_ping()`

```cpp
bool validateConnection(MYSQL* conn) {
    return mysql_ping(conn) == 0;  // 发送 ping 包检测连接有效性
}
```

- MySQL 5.0 及之前：`mysql_ping` 只检测，不自动重连
- MySQL 5.1+：如果设置了 `MYSQL_OPT_RECONNECT`，会自动重连
- **保守策略**：`mysql_ping` 失败视为连接无效，关闭旧连接，重新 `createConnection()`

### 2.5 自定义 Deleter：连接自动归还

现代 C++ 连接池的核心设计：

```cpp
std::shared_ptr<MySQLConnection> acquire(...) {
    MySQLConnection* raw = /* 从队列取出 */;
    return std::shared_ptr<MySQLConnection>(
        raw,
        [this](MySQLConnection* p) { this->releaseConnection(p); }
    );
}
```

**`shared_ptr` 的第二个参数是自定义删除器**。当引用计数归零时，不调用 `delete`，而是调用 `releaseConnection`，将连接归还到连接池的空闲队列。业务线程用完连接后**无需手动归还**，离开作用域自动完成。

### 2.6 超时等待：`std::chrono`

```cpp
std::shared_ptr<MySQLConnection> acquire(
    std::chrono::milliseconds timeout = std::chrono::milliseconds(5000)
);
```

C++98 中需要手动计算绝对时间（`gettimeofday` + `tv_sec`/`tv_nsec` 进位）。现代 C++ 中 `std::chrono` 提供**类型安全的时间**：
- `std::chrono::seconds(3)`
- `std::chrono::milliseconds(500)`
- `std::chrono::minutes(1)`

`wait_for` 内部自动处理平台差异。

### 2.7 移动语义（Move Semantics）

`MySQLConnection` 类禁用拷贝、启用移动：

```cpp
MySQLConnection(MySQLConnection&& other) noexcept : m_conn(other.m_conn) {
    other.m_conn = nullptr;  // 置空源对象，避免双重释放
}
```

- `noexcept` 告诉编译器移动构造不抛异常，`std::vector` 扩容时会**移动**而非拷贝元素
- 移动语义实现了**零拷贝**的所有权转移

---

## 三、线程池 + 连接池结合架构

### 3.1 架构关系图

```
主线程（生产者）
    │
    ▼
┌─────────────┐     ┌─────────────┐     ┌─────────────────┐
│  ThreadPool │────▶│  工作线程 1  │────▶│ acquire()       │
│  (N 线程)   │     │  工作线程 2  │     │ 执行 SQL        │
│             │     │  ...        │     │ 离开作用域自动归还│
│  Task Queue │────▶│  工作线程 N  │────▶│                 │
└─────────────┘     └─────────────┘     └─────────────────┘
                                                │
                                                ▼
                                        ┌─────────────────┐
                                        │ ConnectionPool  │
                                        │ (max M 连接)     │
                                        │ 空闲队列 + CV    │
                                        └─────────────────┘
                                                │
                                                ▼
                                        ┌─────────────────┐
                                        │  MySQL Server   │
                                        └─────────────────┘
```

### 3.2 结合时的关键设计

- **线程池提交数据库任务**：`threadPool.submit(dbQueryTask, std::ref(connPool), id)`
- **`std::ref` 的作用**：`std::bind` 默认拷贝参数，`ConnectionPool` 不可拷贝，用 `std::ref` 传递引用
- **异常传播**：工作线程的异常被 `packaged_task` 捕获，存储在 `future` 中。主线程调用 `future.get()` 时异常重新抛出

---

## 四、并发原语：互斥锁、原子操作、自旋锁

### 4.1 互斥锁（Mutex）—— 睡眠锁

**本质**：线程获取锁失败时，**从运行态切换到阻塞/睡眠态**，被内核放入等待队列，直到锁释放后由内核唤醒。

**底层实现（Linux futex）**：
1. **用户态快速路径**：先用原子操作 CAS 尝试抢锁，如果空闲直接获得，**不进入内核**
2. **内核态慢速路径**：CAS 失败则调用 `futex` 系统调用，线程挂起
3. **释放时**：如果没有等待线程，用户态直接解锁；如果有，需要 `futex_wake` 唤醒

**适用场景**：临界区较长、等待时间不确定（如线程池任务队列、连接池）。

**代价**：两次上下文切换（挂起 + 唤醒），开销在微秒级。

### 4.2 自旋锁（Spinlock）—— 忙等待锁

**本质**：获取锁失败时，线程**不会睡眠**，而是在用户态不断循环执行原子操作检测锁是否释放。

```cpp
while (!atomic_compare_exchange_weak(&lock, 0, 1)) {
    // 空转，CPU 疯狂读取内存
}
```

**适用场景极其苛刻**：
- 临界区极短（通常只有几条指令，纳秒到微秒级）
- **必须多核**（单核下自旋锁是灾难，持有锁的线程被自旋线程占着 CPU 无法运行）
- 中断上下文（内核中中断处理函数不能睡眠）

**代价**：空转浪费 CPU 周期，加剧**缓存一致性流量**（多个核心轮询同一个缓存行，触发总线锁或 MESI 协议风暴）。

### 4.3 原子操作（Atomic Operation）

**本质**：硬件层面保证**不可分割**的操作。现代 CPU 提供专门指令（如 x86 的 `LOCK` 前缀：`LOCK INC`、`LOCK CMPXCHG`），通过锁总线或锁缓存行确保多核下对同一内存地址的读写串行化。

**常见操作**：
- `load` / `store`：原子读写
- `exchange`：原子交换
- `compare_exchange_weak/strong`：CAS，并发算法基石
- `fetch_add` / `fetch_sub`：原子加减

**编译后的指令**：
```asm
lock xaddl %eax, (%rdi)   ; x86 带 LOCK 前缀的原子加法
```

**为什么线程池用 `std::atomic<bool>` 而不是 `bool + mutex`？**
因为 `m_stop` 只是简单的布尔标志，原子操作：
- 无需加锁，**无上下文切换开销**
- 自带内存屏障语义，编译器不会乱序优化
- 多线程可以并发读取，只有写时才硬件级串行化

**局限**：原子操作只能解决"单个变量"的同步。如果临界区涉及多个变量的状态一致性（如"先修改队列，再修改计数器"），必须用锁。

### 4.4 三者对比

| 维度 | 原子操作 | 自旋锁 | 互斥锁 |
|------|---------|--------|--------|
| 等待方式 | 无等待（瞬间完成） | 忙等待（CPU 空转） | 睡眠阻塞（让出 CPU） |
| 上下文切换 | 无 | 无 | 有（2 次） |
| 适用临界区 | 单个变量读/写/加减 | 极短（几行指令） | 较长（系统调用、IO、复杂计算） |
| 多核要求 | 无 | 必须多核 | 无 |
| 实现依赖 | 硬件指令（LOCK 前缀） | 原子操作 + 循环 | 原子操作 + 内核调度（futex） |
| 典型应用 | 引用计数、标志位、无锁队列 | 内核中断处理、极短临界区 | 线程池任务队列、连接池 |

---

## 五、BLOB 与图片二进制存储

### 5.1 什么是 BLOB？

**BLOB** = **Binary Large Object**，数据库专门为存储**无结构二进制数据**设计的字段类型。

数据库常规字段（`VARCHAR`、`INT`）存储结构化文本或数值。但图片、音频、PDF 等本质上是**一长串无意义的字节序列**，没有字符编码、没有换行符语义。

BLOB 告诉数据库：**"不要把这些字节当作文本解析，不要尝试字符集转换，原样存储、原样取出。"**

### 5.2 MySQL BLOB 家族

| 类型 | 最大容量 | 实际用途 |
|------|---------|---------|
| `TINYBLOB` | 255 字节 | 短哈希值 |
| `BLOB` | 64 KB | 小图标、缩略图 |
| `MEDIUMBLOB` | 16 MB | 常规图片、PDF、音频片段 |
| `LONGBLOB` | 4 GB | 高清图片、视频、大型文件 |

**选型原则**：在满足需求的前提下选最小的。BLOB 字段越大，单行数据越宽，缓冲池能缓存的行数越少，查询性能越差。

### 5.3 BLOB vs TEXT

| 维度 | BLOB | TEXT |
|------|------|------|
| 存储内容 | 二进制字节流 | 文本字符串 |
| 字符集 | 无字符集，不转换 | 有字符集（utf8mb4），存储时编码、读取时解码 |
| 比较排序 | 按字节值（binary） | 按字符排序规则（collation） |
| 内容可读性 | 不可读（乱码） | 人类可读 |

**为什么图片不能用 TEXT 存？**

假设 PNG 图片开头是 `0x89 0x50 0x4E 0x47`：
- 存入 `TEXT`，MySQL 尝试按字符集解码。`0x89` 不是合法 UTF-8 序列，会被替换为 `?` 或乱码
- 存入 `BLOB`，MySQL 不做任何解码，原样保存和返回

### 5.4 底层存储原理（InnoDB）

1. **行内存储（Inline）**：BLOB 较小（通常 < 768 字节）时，直接存放在数据页中
2. **行外存储（Off-page）**：BLOB 较大时，行内只存一个 **20 字节的指针**，指向独立的**溢出页（Overflow Page）**。真正的二进制数据存放在溢出页中

### 5.5 两种架构模式对比

| 方案 | 数据库存二进制 | 文件系统存文件 + 数据库存路径 |
|------|---------------|------------------------------|
| 一致性 | 事务内原子完成，不会丢失引用 | 文件与记录可能不一致 |
| 备份 | 数据库 dump 即可，简单 | 需要文件系统 + 数据库双份备份 |
| 性能 | 数据库 IO 压力大，查询慢 | 文件系统直接读，数据库只存轻量元数据 |
| 适用场景 | 小图片（< 1MB）、强一致性要求 | 大文件、高并发读取、CDN 分发 |

**工程共识**：除非图片极小或强事务要求，**优先文件系统 + 存路径**。

### 5.6 二进制存储核心流程

1. **客户端**：以 `rb`（read binary）模式读取图片到内存缓冲区
2. **传输层**：使用 **Prepared Statement**（预编译语句），将二进制缓冲区作为参数绑定
3. **服务端**：MySQL 将字节流原样写入 BLOB 字段

**为什么必须用 Prepared Statement？**

如果拼接 SQL：`INSERT ... VALUES('name', '` + buffer + `')`，二进制数据中的 `0x00`（空字节）会被 C 字符串截断，单引号会破坏 SQL 语法。Prepared Statement 把 SQL 文本和数据**完全分离**，数据以原始字节流传输。

### 5.7 核心代码：Prepared Statement 插入 BLOB

```cpp
// 读取图片到缓冲区
FILE* fp = fopen("avatar.png", "rb");
fseek(fp, 0, SEEK_END);
unsigned long fileSize = ftell(fp);
fseek(fp, 0, SEEK_SET);
unsigned char* buffer = new unsigned char[fileSize];
fread(buffer, 1, fileSize, fp);
fclose(fp);

// 使用 Prepared Statement 插入
MYSQL_STMT* stmt = mysql_stmt_init(mysql);
mysql_stmt_prepare(stmt, "INSERT INTO images(name, data) VALUES(?, ?)", ...);

MYSQL_BIND bind[2] = {};
memset(bind, 0, sizeof(bind));

// 参数1：name
char name[] = "avatar.png";
unsigned long nameLen = strlen(name);
bind[0].buffer_type = MYSQL_TYPE_STRING;
bind[0].buffer = name;
bind[0].buffer_length = nameLen;
bind[0].length = &nameLen;

// 参数2：data（二进制关键）
bind[1].buffer_type = MYSQL_TYPE_BLOB;
bind[1].buffer = buffer;
bind[1].buffer_length = fileSize;
bind[1].length = &fileSize;

mysql_stmt_bind_param(stmt, bind);
mysql_stmt_execute(stmt);
mysql_stmt_close(stmt);
delete[] buffer;
```

---

## 六、RAII 与资源管理

### 6.1 RAII 核心思想

**资源获取即初始化（Resource Acquisition Is Initialization）**：将资源的生命周期绑定到对象的生命周期，构造函数获取资源，析构函数释放资源。

### 6.2 现代 C++ 中的 RAII 工具

| 工具 | 管理资源 | 所有权 |
|------|---------|--------|
| `std::unique_ptr` | 动态内存 | 独占所有权，不可拷贝，可移动 |
| `std::shared_ptr` | 动态内存 | 共享所有权，引用计数 |
| `std::lock_guard` | 互斥锁 | 构造时加锁，析构时解锁 |
| `std::unique_lock` | 互斥锁 | 更灵活，支持中途解锁，配合条件变量 |

### 6.3 连接池中的 RAII

```cpp
{
    auto conn = pool.acquire();  // 获取 shared_ptr
    mysql_query(conn->raw(), "SELECT ...");  // 使用
    // 离开作用域，conn 的 shared_ptr 析构
    // → 自定义 deleter 调用 releaseConnection
    // → 连接自动归还到连接池
}
```

C++98 中必须手动调用 `pool.releaseConnection(conn)`，忘记调用即泄漏。现代 C++ 中**离开作用域自动完成**，异常安全。

---

## 七、编译与调试要点

### 7.1 编译命令

```bash
# C++98 + pthread + MySQL
g++ -std=c++98 -Wall -Wextra -pthread -o pool main.cpp $(mysql_config --cflags --libs)

# 现代 C++（C++14）
g++ -std=c++14 -Wall -Wextra -pthread -o modern_pool main.cpp $(mysql_config --cflags --libs)
```

### 7.2 Debug 必备参数

```bash
# -g：生成调试符号（GDB 定位源码行号）
# -O0：关闭优化（防止变量被寄存器化、代码重排导致断点错位）
g++ -std=c++14 -g -O0 -pthread ...
```

### 7.3 GDB 多线程调试

```gdb
(gdb) info threads          # 查看所有线程
(gdb) thread 2              # 切换到线程 2
(gdb) bt                    # 查看调用栈
(gdb) set scheduler-locking on   # 只运行当前线程，其他冻结
(gdb) set scheduler-locking off  # 恢复所有线程
```

### 7.4 Valgrind 内存检测

```bash
valgrind --leak-check=full --show-leak-kinds=all ./pool
```

**预期结果**：
```text
All heap blocks were freed -- no leaks are possible
```

---

## 八、学习路径总结

1. **先理解 C++98 底层实现**：看清 `pthread_create`、`pthread_mutex_lock`、`pthread_cond_wait` 的系统调用本质
2. **再理解现代 C++ 封装**：体会 `std::thread`、`std::mutex`、`std::condition_variable` 如何用 RAII 消除手动资源管理
3. **对比两种版本**：观察同样的功能，现代 C++ 代码量减少、安全性提高、异常安全天然具备
4. **掌握并发原语**：理解互斥锁、自旋锁、原子操作的适用边界和性能权衡
5. **理解数据库二进制存储**：掌握 BLOB 本质、Prepared Statement 必要性、以及"存文件 vs 存路径"的架构抉择
