struct gs_ctx;
struct gs_msg;

void gs_mq_init();

void gs_mq_insert(struct gs_ctx *ctx, struct gs_msg * msg);
void gs_mq_push(struct gs_ctx *ctx, struct gs_msg * msg);
int gs_mq_pop(struct gs_ctx *ctx, struct gs_msg *msg);

void gs_globalmq_push(struct gs_ctx * ctx);
struct gs_ctx *gs_globalmq_pop();
