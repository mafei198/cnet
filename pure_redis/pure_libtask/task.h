/* Copyright (c) 2005 Russ Cox, MIT; see COPYRIGHT */

#ifndef _TASK_H_
#define _TASK_H_ 1

#ifdef __cplusplus
extern "C" {
#endif

#include <stdarg.h>
#include <inttypes.h>
#include <sys/types.h>

/*
 * basic procs and threads
 */

typedef struct Task Task;

void contextswitch(Task *from, Task *to);
Task* taskalloc(void (*fn)(void*), void *arg, uint stack);
void		needstack(int);

#ifdef __cplusplus
}
#endif
#endif

