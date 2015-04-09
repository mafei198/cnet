#include <stdlib.h>
#include <stdio.h>
#include "redis_data_structure/redis.h"
#include "gs.h"
#include "gs_mq.h"
#include "gs_actor.h"

static int ACTOR_ID = 0;
static gs_ctx *CONTAINER[10];

extern redisClient *sharedRDB;

void register_ctx(const char *name, gs_ctx *ctx) {
    LOCK(sharedRDB->lock);
    CONTAINER[ctx->id] = ctx;
    redisCommand(sharedRDB, "SET %s %ld", name, (intptr_t)ctx);
    UNLOCK(sharedRDB->lock);
}

gs_ctx *gs_ctx_by_name(const char *name) {
    LOCK(sharedRDB->lock);
    redisCommand(sharedRDB, "GET %s", name);
    gs_ctx *ctx = (gs_ctx *)atol(sharedRDB->response->str);
    UNLOCK(sharedRDB->lock);
    return ctx;
}

void callback(void *arg) {
    gs_ctx *ctx = arg;
    gs_msg *msg = ctx->current_msg;
    while (1) {
        void *data = ctx->cb(ctx, msg);
        if (msg->type == MSG_TYPE_CALL) {
            gs_actor_send_msg(ctx->name, msg->from, data, MSG_TYPE_REPLY);
        }
        gs_coro_transfer(ctx->corotine, ctx->main_coroutine);
    }
}

gs_ctx *gs_actor_create(const char *name, void*(* cb)(gs_ctx *, gs_msg *)) {
    gs_ctx *ctx = malloc(sizeof(gs_ctx));
    ctx->id = __sync_fetch_and_add(&ACTOR_ID, 1);
    ctx->name = name;
    ctx->status = CTX_STATUS_OUT_GLOBAL;
    ctx->cb = cb;
    ctx->head = 0;
    ctx->tail = 0;
    ctx->capacity = DEFAULT_MQ_SIZE;
    ctx->lock = 0;
    ctx->queue = malloc(sizeof(gs_msg) * ctx->capacity);
    ctx->next = NULL;
    ctx->corotine = gs_coro_create(callback, ctx, 1024);
    
    register_ctx(name, ctx);
    return ctx;
}

void gs_actor_destroy(gs_ctx *ctx) {
    free(ctx->queue);
    free(ctx);
}

gs_msg *gs_actor_send_msg(const char *from, const char *to, void *data, char type) {
    gs_ctx *target_ctx = gs_ctx_by_name(to);
    
    gs_msg *msg = malloc(sizeof(gs_msg));
    msg->from = from;
    msg->to = to;
    msg->type = type;
    msg->data = data;
    if (type == MSG_TYPE_REPLY) {
        gs_mq_insert(target_ctx, msg);
    }else{
        gs_mq_push(target_ctx, msg);
    }
    return msg;
}

void *gs_actor_call(gs_ctx *ctx, const char *target, void *data) {
    gs_actor_send_msg(ctx->name, target, data, MSG_TYPE_CALL);
    ctx->status = CTX_STATUS_WAIT_REPLY;
    gs_coro_transfer(ctx->corotine, ctx->main_coroutine);
    return ctx->current_msg->data;
}

void gs_actor_cast(gs_ctx *ctx, const char *target, void *data) {
    gs_actor_send_msg(ctx->name, target, data, MSG_TYPE_CAST);
}

void gs_actor_handle_msg(gs_ctx *ctx) {
    gs_msg *msg = malloc(sizeof(gs_msg));
    int is_empty = gs_mq_pop(ctx, msg);
    
    if (msg) {
        ctx->current_msg = msg;
        gs_coro_transfer(ctx->main_coroutine, ctx->corotine);
    }
    free(msg);
    ctx->current_msg = NULL;
    
    if (ctx->status == CTX_STATUS_BE_BUSY) {
        if (!is_empty) {
            gs_globalmq_push(ctx);
        }else{
            LOCK(ctx->lock);
            if (ctx->head == ctx->tail) {
                ctx->status = CTX_STATUS_OUT_GLOBAL;
            }else{
                gs_globalmq_push(ctx);
            }
            UNLOCK(ctx->lock);
        }
    }else{
        assert(ctx->status == CTX_STATUS_WAIT_REPLY);
    }
}
