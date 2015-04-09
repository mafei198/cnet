//
//  gs_coroutine.h
//  pure_redis
//
//  Created by savin on 4/9/15.
//  Copyright (c) 2015 savin. All rights reserved.
//

#ifdef LIBCORO
#include "coro.h"
#else
#include "pure_libtask/task.h"
typedef Task* coro_context;
#endif

void gs_coro_transfer(coro_context from, coro_context to);
void gs_coro_switch(coro_context from, coro_context to);
coro_context gs_coro_create(void (*callback)(void *), void *arg, int stack_size);
