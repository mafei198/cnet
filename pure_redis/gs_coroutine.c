//
//  gs_coroutine.c
//  pure_redis
//
//  Created by savin on 4/9/15.
//  Copyright (c) 2015 savin. All rights reserved.
//

#include "gs_coroutine.h"

#ifdef LIBCORO
coro_context gs_coro_create(void (*callback)(void *), void *arg, int stack_size)
{
    struct coro_stack stack;
    coro_stack_alloc(&stack, stack_size);
    coro_context ctx;
    coro_create(&ctx, callback, arg, stack.sptr, stack.ssze);
    return ctx;
}

#else
Task *gs_coro_create(void (*callback)(void *), void *arg, int stack_size)
{
    return taskalloc(callback, arg, stack_size);
}
#endif
