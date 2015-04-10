//
//  gs_coroutine.h
//  pure_redis
//
//  Created by savin on 4/9/15.
//  Copyright (c) 2015 savin. All rights reserved.
//

#ifdef LIBCORO
#include "coro.h"
coro_context gs_coro_create(void (*callback)(void *), void *arg, int stack_size);
#elif LIBTASK
#include "pure_libtask/task.h"
Task *gs_coro_create(void (*callback)(void *), void *arg, int stack_size);
#endif