# cnet
cnet是一个利用纯C实现的actor模式服务器引擎。

###actor结构
  每个actor为一个结构体实例其中包含：actor_id,actor_name, msg_queue。
###actor之间的通信
  actor之间通信就是往对方的邮箱投递信息,投递方式分:异步和同步,同步投递消息会block当前actor的执行环境并等待对方reply,异步投递会立刻返回当前执行环境继续执行。
###coroutine
  actor同步call不阻塞当前worker，因为每个actor的都被包裹在一个coroutine的执行环境内，当调用gs_actor_call时我们将当前执行权限交回worker，等msg_queue收到reply时我们在resume被阻塞的actor让它继续执行。
  
###actor调度原理
1. 当main函数启动后：会根据当前服务器核心数分配对应数量的worker线程（pthread）
2. 启动的线程利用pthread_cond_wait阻塞在全局执行队列
3. 当actor的msg_queue收到消息后，会把actor放进全局执行队列并通知被阻塞的worker线程，唤醒的worker从全局队列pop出actor并调用其callback函数
