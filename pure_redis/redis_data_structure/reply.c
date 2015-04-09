#include "redis.h"

redisReply * createReply(int type);
int addReplyString(redisClient *c, char *s, size_t len);

size_t zmalloc_size_sds(sds s) {
    return zmalloc_size(s-sizeof(struct sdshdr));
}

void *dupClientReplyValue(void *o) {
    incrRefCount((robj*)o);
    return o;
}


/* Create a duplicate of the last object in the reply list when
 * it is not exclusively owned by the reply list. */
robj *dupLastObjectIfNeeded(list *reply) {
    robj *new, *cur;
    listNode *ln;
    redisAssert(listLength(reply) > 0);
    ln = listLast(reply);
    cur = listNodeValue(ln);
    if (cur->refcount > 1) {
        new = dupStringObject(cur);
        decrRefCount(cur);
        listNodeValue(ln) = new;
    }
    return listNodeValue(ln);
}


/* -----------------------------------------------------------------------------
 * Low level functions to add more data to output buffers.
 * -------------------------------------------------------------------------- */

int _addReplyToBuffer(redisClient *c, redisReply *r) {
    if (c->response == NULL) {
        c->response = r;
        if (r->type == REDIS_REPLY_ARRAY) {
            size_t len = r->elements;
            if (len == 0) {
                len = 1;
            }
            r->elements = len;
            r->element = zmalloc(len * sizeof(redisReply*));
        }
        return REDIS_OK;
    }
    
    redisReply *parent = c->response;
    assert(parent->type == REDIS_REPLY_ARRAY);
    
    if (parent->added == parent->elements) {
        parent->element = zrealloc(parent->element, sizeof(redisReply *) * parent->added * 2);
        parent->elements = parent->added * 2;
    }
    
    parent->element[parent->added++] = r;
    if (parent->elements < parent->added) {
        parent->elements = parent->added;
    }

    return REDIS_OK;
}

/* -----------------------------------------------------------------------------
 * Higher level functions to queue data on the client output buffer.
 * The following functions are the ones that commands implementations will call.
 * -------------------------------------------------------------------------- */

void addReply(redisClient *c, robj *obj) {
    /* This is an important place where we can avoid copy-on-write
     * when there is a saving child running, avoiding touching the
     * refcount field of the object if it's not needed.
     *
     * If the encoding is RAW and there is room in the static buffer
     * we'll be able to send the object to the client without
     * messing with its page. */
    if (obj->encoding == REDIS_ENCODING_RAW) {
        addReplyString(c, obj->ptr, sdslen(obj->ptr));
    } else if (obj->encoding == REDIS_ENCODING_INT) {
        /* Optimization: if there is room in the static buffer for 32 bytes
         * (more than the max chars a 64 bit integer can take as string) we
         * avoid decoding the object and go for the lower level approach. */
        if (listLength(c->reply) == 0 && (sizeof(c->buf) - c->bufpos) >= 32) {
            char buf[32];
            int len;

            len = ll2string(buf,sizeof(buf),(long)obj->ptr);
            addReplyString(c, buf, len);
            return;
            /* else... continue with the normal code path, but should never
             * happen actually since we verified there is room. */
        }
        obj = getDecodedObject(obj);
        addReplyString(c, obj->ptr, sdslen(obj->ptr));
        decrRefCount(obj);
    } else {
        redisPanic("Wrong obj->encoding in addReply()");
    }
}

void addReplySds(redisClient *c, sds s) {
    addReplyString(c, s, sdslen(s));
    sdsfree(s);
}

int addReplyString(redisClient *c, char *s, size_t len) {
    redisReply *r = createReply(REDIS_REPLY_STRING);
    r->len = len;
    r->str = zmalloc(len+1);
    strcpy(r->str, s);
    return _addReplyToBuffer(c, r);
}

void addReplyErrorLength(redisClient *c, char *s, size_t len) {
    redisReply *r = createReply(REDIS_REPLY_ERROR);
    r->len = len;
    r->str = zmalloc(len+1);
    strcpy(r->str, s);
    _addReplyToBuffer(c, r);
}

void addReplyError(redisClient *c, char *err) {
    addReplyErrorLength(c,err,strlen(err));
}

void addReplyErrorFormat(redisClient *c, const char *fmt, ...) {
    size_t l, j;
    va_list ap;
    va_start(ap,fmt);
    sds s = sdscatvprintf(sdsempty(),fmt,ap);
    va_end(ap);
    /* Make sure there are no newlines in the string, otherwise invalid protocol
     * is emitted. */
    l = sdslen(s);
    for (j = 0; j < l; j++) {
        if (s[j] == '\r' || s[j] == '\n') s[j] = ' ';
    }
    addReplyErrorLength(c,s,sdslen(s));
    sdsfree(s);
}

void addReplyStatusLength(redisClient *c, char *s, size_t len) {
    redisReply *r = createReply(REDIS_REPLY_STATUS);
    r->len = len;
    r->str = zmalloc(len+1);
    strcpy(r->str, s);
    _addReplyToBuffer(c, r);
}

void addReplyStatus(redisClient *c, char *status) {
    addReplyStatusLength(c,status,strlen(status));
}

void addReplyStatusFormat(redisClient *c, const char *fmt, ...) {
    va_list ap;
    va_start(ap,fmt);
    sds s = sdscatvprintf(sdsempty(),fmt,ap);
    va_end(ap);
    addReplyStatusLength(c,s,sdslen(s));
    sdsfree(s);
}

/* Adds an empty object to the reply list that will contain the multi bulk
 * length, which is not known when this function is called. */
void *addDeferredMultiBulkLength(redisClient *c) {
    redisReply *r = createReply(REDIS_REPLY_ARRAY);
    r->elements = 0;
    r->added = 0;
    r->element = NULL;
    r->integer = 0;
    r->len = 0;
    r->str = NULL;
    _addReplyToBuffer(c, r);
    
    /* Note that we install the write event here even if the object is not
     * ready to be sent, since we are sure that before returning to the
     * event loop setDeferredMultiBulkLength() will be called. */
    listAddNodeTail(c->reply,createObject(REDIS_STRING,NULL));
    return listLast(c->reply);
}

/* Populate the length object and try gluing it to the next chunk. */
void setDeferredMultiBulkLength(redisClient *c, void *node, long length) {
    listNode *ln = (listNode*)node;
    robj *len, *next;

    /* Abort when *node is NULL (see addDeferredMultiBulkLength). */
    if (node == NULL) return;

    len = listNodeValue(ln);
    len->ptr = sdscatprintf(sdsempty(),"*%ld\r\n",length);
    c->reply_bytes += zmalloc_size_sds(len->ptr);
    if (ln->next != NULL) {
        next = listNodeValue(ln->next);

        /* Only glue when the next node is non-NULL (an sds in this case) */
        if (next->ptr != NULL) {
            c->reply_bytes -= zmalloc_size_sds(len->ptr);
            c->reply_bytes -= zmalloc_size_sds(next->ptr);
            len->ptr = sdscatlen(len->ptr,next->ptr,sdslen(next->ptr));
            c->reply_bytes += zmalloc_size_sds(len->ptr);
            listDelNode(c->reply,ln->next);
        }
    }
}

/* Add a double as a bulk reply */
void addReplyDouble(redisClient *c, double d) {
    char dbuf[128], sbuf[128];
    int dlen, slen;
    dlen = snprintf(dbuf,sizeof(dbuf),"%.17g",d);
    slen = snprintf(sbuf,sizeof(sbuf),"$%d\r\n%s\r\n",dlen,dbuf);
    addReplyString(c,sbuf,slen);
}

void addReplyLongLong(redisClient *c, long long ll) {
    redisReply *r = createReply(REDIS_REPLY_INTEGER);
    r->integer = ll;
    _addReplyToBuffer(c, r);
}

void addReplyMultiBulkLen(redisClient *c, long length) {
    redisReply *r = createReply(REDIS_REPLY_ARRAY);
    r->elements = length;
    r->added = 0;
    _addReplyToBuffer(c, r);
}

/* Create the length prefix of a bulk reply, example: $2234 */
size_t getReplyBulkLen(redisClient *c, robj *obj) {
    size_t len;

    if (obj->encoding == REDIS_ENCODING_RAW) {
        len = sdslen(obj->ptr);
    } else {
        long n = (long)obj->ptr;

        /* Compute how many bytes will take this integer as a radix 10 string */
        len = 1;
        if (n < 0) {
            len++;
            n = -n;
        }
        while((n = n/10) != 0) {
            len++;
        }
    }
    
    return len;
}

/* Add a Redis Object as a bulk reply */
void addReplyBulk(redisClient *c, robj *obj) {
    size_t len = getReplyBulkLen(c,obj);
    
    redisReply *r = createReply(REDIS_REPLY_STRING);
    r->len = len;
    r->str = zmalloc(len+1);
    
    if (obj->encoding == REDIS_ENCODING_RAW) {
        strcpy(r->str, obj->ptr);
    } else if (obj->encoding == REDIS_ENCODING_INT) {
        /* Optimization: if there is room in the static buffer for 32 bytes
         * (more than the max chars a 64 bit integer can take as string) we
         * avoid decoding the object and go for the lower level approach. */
        if (listLength(c->reply) == 0 && (sizeof(c->buf) - c->bufpos) >= 32) {
            char buf[32];
            int blen;
            
            blen = ll2string(buf,sizeof(buf),(long)obj->ptr);
            assert(len == blen);
            strcpy(r->str, buf);
        } else {
            obj = getDecodedObject(obj);
            strcpy(r->str, obj->ptr);
            decrRefCount(obj);
        }
    } else {
        redisPanic("Wrong obj->encoding in addReply()");
    }
    
    _addReplyToBuffer(c, r);
}

/* Add a C buffer as bulk reply */
void addReplyBulkCBuffer(redisClient *c, void *p, size_t len) {
    redisReply *r = createReply(REDIS_REPLY_STRING);
    r->str = zmalloc(len+1);
    strcpy(r->str, p);
    r->len = (int)len;
    _addReplyToBuffer(c, r);
}

/* Add a C nul term string as bulk reply */
void addReplyBulkCString(redisClient *c, char *s) {
    if (s == NULL) {
        addReply(c,shared.nullbulk);
    } else {
        addReplyBulkCBuffer(c,s,strlen(s));
    }
}

/* Add a long long as a bulk reply */
void addReplyBulkLongLong(redisClient *c, long long ll) {
    redisReply *r = createReply(REDIS_REPLY_INTEGER);
    r->integer = ll;
    _addReplyToBuffer(c, r);
}

redisReply * createReply(int type)
{
    redisReply *r = zmalloc(sizeof(*r));
    assert(r);
    r->type = type;
    return r;
}

/* Free a reply object */
void freeReplyObject(redisReply *reply) {
    redisReply *r = reply;
    size_t j;

    switch(r->type) {
    case REDIS_REPLY_INTEGER:
        break; /* Nothing to free */
    case REDIS_REPLY_ARRAY:
        if (r->element != NULL) {
            for (j = 0; j < r->elements; j++)
                if (r->element[j] != NULL)
                    freeReplyObject(r->element[j]);
            zfree(r->element);
        }
        break;
    case REDIS_REPLY_ERROR:
    case REDIS_REPLY_STATUS:
    case REDIS_REPLY_STRING:
        if (r->str != NULL)
            zfree(r->str);
        break;
    }
    zfree(r);
}

void freeReply(redisClient *c)
{
    if (c->response) {
        freeReplyObject(c->response);
        c->response = NULL;
    }
}

/* Rewrite the command vector of the client. All the new objects ref count
 * is incremented. The old command vector is freed, and the old objects
 * ref count is decremented. */
void rewriteClientCommandVector(redisClient *c, int argc, ...) {
    va_list ap;
    int j;
    robj **argv; /* The new argument vector */

    argv = zmalloc(sizeof(robj*)*argc);
    va_start(ap,argc);
    for (j = 0; j < argc; j++) {
        robj *a;
        
        a = va_arg(ap, robj*);
        argv[j] = a;
        incrRefCount(a);
    }
    /* We free the objects in the original vector at the end, so we are
     * sure that if the same objects are reused in the new vector the
     * refcount gets incremented before it gets decremented. */
    for (j = 0; j < c->argc; j++) decrRefCount(c->argv[j]);
    zfree(c->argv);
    /* Replace argv and argc with our new versions. */
    c->argv = argv;
    c->argc = argc;
    c->cmd = lookupCommandOrOriginal(c->argv[0]->ptr);
    redisAssertWithInfo(c,NULL,c->cmd != NULL);
    va_end(ap);
}


/* Rewrite a single item in the command vector.
 * The new val ref count is incremented, and the old decremented. */
void rewriteClientCommandArgument(redisClient *c, int i, robj *newval) {
    robj *oldval;
   
    redisAssertWithInfo(c,NULL,i < c->argc);
    oldval = c->argv[i];
    c->argv[i] = newval;
    incrRefCount(newval);
    decrRefCount(oldval);

    /* If this is the command name make sure to fix c->cmd. */
    if (i == 0) {
        c->cmd = lookupCommandOrOriginal(c->argv[0]->ptr);
        redisAssertWithInfo(c,NULL,c->cmd != NULL);
    }
}

static void freeClientArgv(redisClient *c) {
    int j;
    for (j = 0; j < c->argc; j++)
        decrRefCount(c->argv[j]);
    c->argc = 0;
    c->cmd = NULL;
}

void freeClient(redisClient *c) {
    freeReply(c);
    
    listRelease(c->reply);
    freeClientArgv(c);

    /* Release other dynamically allocated client structure fields,
     * and finally release the client structure itself. */
    if (c->name) decrRefCount(c->name);
    
    zfree(c->db->dict);
    zfree(c->db->blocking_keys);
    zfree(c->db->expires);
    zfree(c->db->ready_keys);
    zfree(c->db->watched_keys);
    zfree(c->db);
    
    zfree(c->argv);
    freeClientMultiState(c);
    zfree(c);
}

/* resetClient prepare the client to process the next command */
void resetClient(redisClient *c) {
    freeClientArgv(c);
    /* We clear the ASKING flag as well if we are not inside a MULTI. */
    if (!(c->flags & REDIS_MULTI)) c->flags &= (~REDIS_ASKING);
}

redisReply *redisCommand(redisClient *c, char *format, ...)
{
    freeReply(c);
    va_list ap;
    va_start(ap,format);
    int size = vsnprintf(NULL, 0, format, ap);
    va_end(ap);
    
    char *cmd = zmalloc(size+1);
    va_start(ap,format);
    vsprintf(cmd, format, ap);
    va_end(ap);
    
    c->argc = 0;
    char **toks = zmalloc((size+1) * 2);
    char *tok = strtok(cmd, " ");
    while (tok) {
        toks[c->argc] = tok;
        c->argc++;
        tok = strtok(NULL, " ");
    }
    
    if (c->argv) zfree(c->argv);
    c->argv = zmalloc(sizeof(robj*)*c->argc);
    
    int i;
    for (i=0; i < c->argc; i++) {
        c->argv[i] = createStringObject(toks[i], strlen(toks[i]));
    }
    
    zfree(toks);
    zfree(cmd);
    
    processCommand(c);
    resetClient(c);

    return c->response;
}
