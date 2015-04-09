struct gs_ctx;
struct gs_msg;

struct gs_ctx *gs_actor_create(const char *name, void *(*cb)(gs_ctx *, gs_msg *));
void gs_actor_destroy(struct gs_ctx *ctx);

void *gs_actor_call(struct gs_ctx *ctx, const char *target, void *data);
void gs_actor_cast(struct gs_ctx *ctx, const char *target, void *data);
gs_msg *gs_actor_send_msg(const char *from, const char *target, void *data, char type);

void gs_actor_handle_msg(struct gs_ctx *ctx);

struct gs_ctx *gs_ctx_by_name(const char *name);
