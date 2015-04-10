#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include "gs.h"
#include "gs_mq.h"
#include "gs_actor.h"
#include "redis_data_structure/redis.h"
#include "gs_coroutine.h"

redisClient *sharedRDB;

#ifdef LIBTASK
Task *mainCtx, *workerCtx;
#elif LIBCORO
coro_context mainCtx, workerCtx;
#endif

pthread_cond_t global_queue_ready = PTHREAD_COND_INITIALIZER;
pthread_mutex_t global_queue_lock = PTHREAD_MUTEX_INITIALIZER;

void gs_notify_global_queue()
{
    pthread_cond_signal(&global_queue_ready);
}

void *worker(){
#ifdef LIBTASK
    Task *main_coroutine = gs_coro_create(NULL, NULL, 1024);
#elif LIBCORO
    struct coro_stack stack;
    coro_stack_alloc(&stack, 1024);
    coro_context main_coroutine;
    coro_create(&main_coroutine, NULL, NULL, stack.sptr, stack.ssze);
#endif
    gs_ctx *ctx;
    for(;;) {
        pthread_mutex_lock(&global_queue_lock);
        ctx = gs_globalmq_pop();
        while(ctx == NULL) {
            pthread_cond_wait(&global_queue_ready, &global_queue_lock);
            ctx = gs_globalmq_pop();
//            printf("thread %ud weekup!\n", (unsigned int)pthread_self());
        }
        pthread_mutex_unlock(&global_queue_lock);
        ctx->main_coroutine = main_coroutine;
        gs_actor_handle_msg(ctx);
    }
}

int enter = 0;
void *callbackSavin(gs_ctx *ctx, gs_msg *msg) {
    enter++;
    printf("thread id: %lu\n", (unsigned long)pthread_self());
//    printf("hello from Savin, type: %d, from: %s, to: %s, msg: %s\n",
//           msg->type, msg->from, msg->to, msg->data);
    if (enter == 2) {
        printf("reenter\n");
    }
    printf("handler: %s ", "savin");
    printf("type: %d ", msg->type);
    printf("from: %d ", msg->from);
    printf("to: %d ", msg->to);
    printf("data: %s\n", (char *)msg->data);
    long long int start = mstime();
    long long int times = 1000000;
    int count = 0;
    int target = gs_id_by_name("max");
    while (++count < times) {
//        gs_actor_call(ctx, target, (void *)"3");
        gs_actor_cast(ctx, target, (void *)"3");
    }
    printf("count: %lld\n", times);
    long long int stop = mstime();
    printf("time used: %lldms, %fs\n", stop - start, (float)(stop - start)/1000);
    printf("times per second: %lld\n", (times * 1000/(stop - start)));
    
    return (void *)"savin reply";
}

int ccc = 0;
void *callbackMax(gs_ctx *ctx, gs_msg *msg) {
//    printf("hello from Max, type: %d, from: %s, to: %s, msg: %s\n",
//           ctx->current_msg->type,
//           ctx->current_msg->from,
//           ctx->current_msg->to,
//           (char *)ctx->current_msg->data);
    return (void *)"max reply";
}

void *test() {
    gs_ctx *max = gs_actor_create("max", callbackMax);
    gs_ctx *savin = gs_actor_create("savin", callbackSavin);
    
    gs_actor_send_msg(-1, max->id, "1", MSG_TYPE_CAST);
    gs_actor_send_msg(-1, savin->id, "2", MSG_TYPE_CAST);
    
    return (void *)0;
}

void gs_start(const char *addr, int port) {
    int thread_count = 8;
    pthread_t tids[thread_count];
    int i;
    for(i=0; i < thread_count; i++) {
        if (pthread_create(&tids[i], NULL, worker, NULL)) {
            fprintf(stderr, "Create thread failed!\n");
            exit(1);
        }
    }
    
    sleep(1);
    pthread_t tid;
    pthread_create(&tid, NULL, test, NULL);
    
    for(i=0; i < thread_count; i++) {
        pthread_join(tids[i], NULL);
    }
    
    pthread_join(tid, NULL);
    
}

int main (int argc, char const* argv[])
{
    redisInit();
    sharedRDB = createClient();
    
//    redisClient *c = createClient();
//    long long int start = mstime();
//    long long int times = 1000000;
//    for (int i=0; i<times; i++) {
//        redisCommand(c, "SET ts_%d %d", i, i);
//        redisCommand(c, "GET ts_%d", i);
//    }
//    long long int stop = mstime();
//    printf("time used: %lldms, %fs\n", stop - start, (float)(stop - start)/1000);
//    printf("times per second: %lld\n", (times * 1000/(stop - start)));
    
    const char *addr = "127.0.0.1";
    int port = 3000;
    gs_mq_init();
    gs_start(addr, port);
    
    return 0;
}
