#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <assert.h>

#include "gs.h"
#include "gs_mq.h"

gs_global_queue *QUEUE;

extern pthread_cond_t global_queue_ready;

void gs_mq_init() {
    gs_global_queue *q = malloc(sizeof(gs_global_queue));
    q->head = NULL;
    q->tail = NULL;
    q->lock = 0;
    QUEUE = q;
}

static int msg_poped, msg_pushed;

void gs_globalmq_push(gs_ctx * ctx) {
    gs_global_queue *q = QUEUE;
    
    LOCK(q->lock);
    ctx->status = CTX_STATUS_IN_GLOBAL;
    if (q->tail) {
        assert(q->head);
        q->tail->next = ctx;
        q->tail = ctx;
    } else {
        q->head = q->tail = ctx;
    }
    UNLOCK(q->lock);
    pthread_cond_signal(&global_queue_ready);
}

gs_ctx *gs_globalmq_pop() {
    gs_global_queue *q = QUEUE;
    
    LOCK(q->lock);
    gs_ctx *ctx = q->head;
    if (ctx) {
        q->head = ctx->next;
        if (q->head == NULL) {
            assert(ctx == q->tail);
            q->tail = NULL;
        }
        ctx->next = NULL;
        ctx->status = CTX_STATUS_BE_BUSY;
    }else{
        assert(!q->tail);
    }
    UNLOCK(q->lock);
    
    return ctx;
}

static void expand_queue(gs_ctx *ctx) {
    gs_msg *queue = malloc(sizeof(gs_msg) * ctx->capacity * 2);
    
    int i;
    for(i=0; i < ctx->capacity; i++) {
        queue[i] = ctx->queue[(ctx->head + i) % ctx->capacity];
    }
    
    ctx->head = 0;
    ctx->tail = ctx->capacity;
    ctx->capacity *= 2;
    
    free(ctx->queue);
    ctx->queue = queue;
}

void gs_mq_push(gs_ctx *ctx, gs_msg *msg) {
    LOCK(ctx->lock);
    ctx->queue[ctx->tail] = *msg;
    
    if (++ctx->tail >= ctx->capacity) {
        ctx->tail = 0;
    }
    
    if (ctx->tail == ctx->head) {
        expand_queue(ctx);
    }
    
    int len;
    if (ctx->tail > ctx->head) {
        len = ctx->tail - ctx->head;
    } else {
        len = ctx->tail + ctx->capacity - ctx->head;
    }
    
    if (len == 1 && ctx->status == CTX_STATUS_OUT_GLOBAL) {
        gs_globalmq_push(ctx);
    }
    UNLOCK(ctx->lock);
}

void gs_mq_insert(gs_ctx *ctx, gs_msg *msg) {
    LOCK(ctx->lock);
    
    int len;
    if (ctx->tail > ctx->head) {
        len = ctx->tail - ctx->head;
    } else {
        len = ctx->tail + ctx->capacity - ctx->head;
    }
    
    if (len+1 == ctx->capacity) {
        expand_queue(ctx);
    }
    
    if (ctx->head == 0) {
        ctx->head = ctx->capacity - 1;
    }else{
        --ctx->head;
    }
    ctx->queue[ctx->head] = *msg;
    
    if (ctx->status == CTX_STATUS_OUT_GLOBAL) {
        gs_globalmq_push(ctx);
    }
    UNLOCK(ctx->lock);
}

int gs_mq_pop(gs_ctx *ctx, gs_msg *msg) {
    LOCK(ctx->lock);
    int is_empty = 0;
    if (ctx->head != ctx->tail) {
        *msg = ctx->queue[ctx->head];
        
        if (++ctx->head >= ctx->capacity) {
            ctx->head = 0;
        }
        
        if (ctx->head == ctx->tail) {
            ctx->head = 0;
            ctx->tail = 0;
            gs_msg *queue = malloc(sizeof(gs_msg) * DEFAULT_MQ_SIZE);
            free(ctx->queue);
            ctx->queue = queue;
            is_empty = 1;
        }
    } else {
        is_empty = 1;
    }
    UNLOCK(ctx->lock);
    
    return is_empty;
}
