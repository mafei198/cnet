#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include "gs.h"
#include "gs_mq.h"
#include "gs_actor.h"
#include "redis_data_structure/redis.h"
#include "gs_coroutine.h"

redisClient *sharedRDB;

coro_context *mainCtx, *workerCtx;

pthread_cond_t global_queue_ready = PTHREAD_COND_INITIALIZER;
pthread_mutex_t global_queue_lock = PTHREAD_MUTEX_INITIALIZER;

void gs_notify_global_queue()
{
    pthread_cond_signal(&global_queue_ready);
}

void *worker(){
    coro_context *main_coroutine = gs_coro_create(NULL, NULL, 1024);
    gs_ctx *ctx;
    for(;;) {
        pthread_mutex_lock(&global_queue_lock);
        ctx = gs_globalmq_pop();
        while(ctx == NULL) {
            printf("worker wait\n");
            pthread_cond_wait(&global_queue_ready, &global_queue_lock);
            ctx = gs_globalmq_pop();
        }
        pthread_mutex_unlock(&global_queue_lock);
        ctx->main_coroutine = main_coroutine;
        if (strcmp(ctx->queue[0].data, "3") == 0) {
            printf("last msg\n");
        }
        gs_actor_handle_msg(ctx);
        printf("handled\n");
    }
}

void *callbackSavin(gs_ctx *ctx, gs_msg *msg) {
    printf("hello from Savin, type: %d, from: %s, to: %s, msg: %s\n",
           msg->type, msg->from, msg->to,
           (char *)ctx->current_msg->data);
    gs_actor_cast(ctx, "max", (void *)"3");
    return (void *)"savin reply";
}

void *callbackMax(gs_ctx *ctx, gs_msg *msg) {
    printf("hello from Max, type: %d, from: %s, to: %s, msg: %s\n",
           ctx->current_msg->type,
           ctx->current_msg->from,
           ctx->current_msg->to,
           (char *)ctx->current_msg->data);
    return (void *)"max reply";
}

void *test() {
    gs_actor_create("max", callbackMax);
    gs_actor_create("savin", callbackSavin);
    
    gs_actor_send_msg("fake", "max", "1", MSG_TYPE_CAST);
    gs_actor_send_msg("fake", "savin", "2", MSG_TYPE_CAST);
    
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

int count = 0;
void worker_coro(void *arg)
{
//    while(1) {
//        ++count;
//        if(count > 1800000) {
//            break;
//        }
//        gs_coro_transfer(workerCtx, mainCtx);
//    }
    printf("hello from worker!\n");
    gs_coro_transfer(workerCtx, mainCtx);
}

int main (int argc, char const* argv[])
{
    printf("-----------test redis data structure----------\n");
    redisInit();
    sharedRDB = createClient();
    
//    redisClient *c = createClient();
//    char *a = "hello";
//    intptr_t i = (intptr_t)a;
//    redisCommand(c, "SET aa %lu", i);
//    redisCommand(c, "GET aa");
//    printf("a: %s\n", (char *)atoll(c->response->str));
    
//    redisCommand(c, "SET ts_%d %s", 1, "a");
//    redisReply *r = redisCommand(c, "GET ts_%d", 1);
//    printf("reply: %s\n", r->str);
//    printf("reply: %d\n", atoi(r->str));
//    printf("reply: %d\n", r->type);
    
//    long long int start = mstime();
//    long long int times = 1000000;
//    for (int i=0; i<times; i++) {
//        redisCommand(c, "SET ts_%d %d", i, i);
//        redisCommand(c, "GET ts_%d", i);
//    }
//    long long int stop = mstime();
//    printf("time used: %lldms, %fs\n", stop - start, (float)(stop - start)/1000);
//    printf("times per second: %lld\n", (times * 1000/(stop - start)));
    
//    redisCommand(c, "MULTI");
//    redisCommand(c, "SADD max a");
//    redisCommand(c, "SADD max b");
//    redisCommand(c, "SADD max c");
//    redisCommand(c, "SADD max d");
//    redisCommand(c, "EXEC");
//    redisReply *reply = redisCommand(c, "SMEMBERS max");
//    for (int i = 0; i < reply->elements; ++i)
//    {
//        printf("reply: %s\n", reply->element[i]->str);
//    }
//    
//    freeClient(c);
//    printf("-----------test redis data structure----------\n");
    
//    return 0;
    const char *addr = "127.0.0.1";
    int port = 3000;
    gs_mq_init();
    gs_start(addr, port);
    
    int idx[10];
    
//    mainCtx = malloc(sizeof(coro_context *));
//    workerCtx = malloc(sizeof(coro_context *));
//    gs_coro_create(mainCtx, NULL, NULL, 1024);
//    gs_coro_create(workerCtx, worker_coro, &idx[0], 1024);
//    
//    gs_coro_transfer(mainCtx, workerCtx);
    
//    long long int start = mstime();
//    long long int times = 1800001;
//    int cc = 0;
//    while(1) {
//        ++cc;
//        if(cc > 1800001) {
//            break;
//        }
//        gs_coro_transfer(mainCtx, workerCtx);
//    }
//    
    printf("hello from main\n");
//    long long int stop = mstime();
//    printf("time used: %lldms, %fs\n", stop - start, (float)(stop - start)/1000);
//    printf("times per second: %lld\n", (times * 1000/(stop - start)));
    
    return 0;
}
