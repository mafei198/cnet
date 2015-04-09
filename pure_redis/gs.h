#include "gs_coroutine.h"

typedef struct gs_msg {
  const char *from; // msg sender
  const char *to; // msg receiver
  int type; // call,cast
  void *data;
} gs_msg;

typedef struct gs_ctx {
    int id;
    const char *name;
    int status;
    void *(*cb)(struct gs_ctx *, gs_msg *);
    int head;
    int tail;
    int capacity;
    int lock;
    gs_msg *queue;
    gs_msg *current_msg;
    struct gs_ctx *next;
    coro_context *main_coroutine;
    coro_context *corotine;
} gs_ctx;

typedef void *(*gs_cb)(struct gs_ctx *ctx, gs_msg *msg);

typedef struct gs_global_queue {
  gs_ctx *head;
  gs_ctx *tail;
  int lock;
} gs_global_queue;

#define CTX_STATUS_IN_GLOBAL  1
#define CTX_STATUS_OUT_GLOBAL 2
#define CTX_STATUS_BE_BUSY    3
#define CTX_STATUS_WAIT_REPLY 4

#define DEFAULT_MQ_SIZE 64

#define LOCK(m) while(__sync_lock_test_and_set(&m, 1)) {}
#define UNLOCK(m) __sync_lock_release(&m)

#define MSG_TYPE_CALL  1
#define MSG_TYPE_CAST  2
#define MSG_TYPE_REPLY 3
