#include <stdlib.h>
#include <stdio.h>
#include <assert.h>
#include "gs.h"
#include "gs_mq.h"
#include "gs_actor.h"
#include "uthash.h"

static int ACTOR_ID = 0;

struct actor {
    int id;
    gs_ctx *ctx;
    UT_hash_handle hh;
};

struct name_map {
    char *name;
    int id;
    UT_hash_handle hh;
};

struct actor *actors = NULL;
struct name_map *named_ids = NULL;
int actor_lock = 0;
int named_lock = 0;

void register_ctx(gs_ctx *ctx) {
    LOCK(actor_lock);
    struct actor *v;
    v = malloc(sizeof(struct actor));
    v->ctx = ctx;
    v->id = ctx->id;
    HASH_ADD_INT(actors, id, v);
    UNLOCK(actor_lock);
    
    LOCK(named_lock);
    struct name_map *value;
    value = malloc(sizeof(struct name_map));
    value->name = malloc(strlen(ctx->name));
    strcpy(value->name, ctx->name);
    value->id = ctx->id;
    HASH_ADD_STR(named_ids, name, value);
    UNLOCK(named_lock);
}

gs_ctx *gs_ctx_by_id(int id) {
    LOCK(actor_lock);
    struct actor *v;
    HASH_FIND_INT(actors, &id, v);
    UNLOCK(actor_lock);
    return v->ctx;
}

int gs_id_by_name(const char *name) {
    LOCK(named_lock);
    struct name_map *v;
    HASH_FIND_STR(named_ids, name, v);
    UNLOCK(named_lock);
    return v->id;
}

void callback(void *arg) {
    gs_ctx *ctx = arg;
    while (1) {
        gs_msg *msg = ctx->current_msg;
        assert(ctx->id == msg->to);
        assert(ctx->id != msg->from);
        int type = msg->type;
        int from = msg->from;
        void *data = ctx->cb(ctx, msg);
        if (type == MSG_TYPE_CALL) {
            assert(from != -1);
            gs_actor_send_msg(ctx->id, from, data, MSG_TYPE_REPLY);
        }
#ifdef LIBCORO
        coro_transfer(&ctx->corotine, &ctx->main_coroutine);
#elif LIBTASK
        contextswitch(ctx->corotine, ctx->main_coroutine);
#endif
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
    
    register_ctx(ctx);
    return ctx;
}

void gs_actor_destroy(gs_ctx *ctx) {
    free(ctx->queue);
    free(ctx);
}

gs_msg gs_actor_send_msg(int from, int to, void *data, char type) {
    assert(from != to);
    gs_ctx *target_ctx = gs_ctx_by_id(to);
    
    gs_msg msg;
    msg.from = from;
    msg.to = to;
    msg.type = type;
//    size_t size = sizeof(*data);
//    msg.data = malloc(size);
//    memcpy(msg.data, data, size);
    msg.data = data;
    if (type == MSG_TYPE_REPLY) {
        gs_mq_insert(target_ctx, &msg);
    }else{
        gs_mq_push(target_ctx, &msg);
    }
    return msg;
}

void *gs_actor_call(gs_ctx *ctx, int target, void *data) {
    ctx->status = CTX_STATUS_WAIT_REPLY;
    gs_actor_send_msg(ctx->id, target, data, MSG_TYPE_CALL);
#ifdef LIBCORO
    coro_transfer(&ctx->corotine, &ctx->main_coroutine);
#elif LIBTASK
    contextswitch(ctx->corotine, ctx->main_coroutine);
#endif
    return ctx->current_msg->data;
}

void gs_actor_cast(gs_ctx *ctx, int target, void *data) {
    gs_actor_send_msg(ctx->id, target, data, MSG_TYPE_CAST);
}

void gs_actor_handle_msg(gs_ctx *ctx) {
    gs_msg *msg = malloc(sizeof(*msg));
    int is_empty = gs_mq_pop(ctx, msg);
    
    if (&msg) {
        ctx->current_msg = msg;
#ifdef LIBCORO
        coro_transfer(&ctx->main_coroutine, &ctx->corotine);
#elif LIBTASK
        contextswitch(ctx->main_coroutine, ctx->corotine);
#endif
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
        LOCK(ctx->lock);
        if (ctx->head == ctx->tail) {
            ctx->status = CTX_STATUS_OUT_GLOBAL;
        }else{
            gs_globalmq_push(ctx);
        }
        UNLOCK(ctx->lock);
    }
}
