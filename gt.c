#include <stdlib.h>
#include <string.h>

#include <stdio.h>

#include "gt.h"

#define PTR   uintptr_t
#define PTRSZ sizeof(PTR)

#if defined(__i386__)
#error i386 is not supported yet
#elif defined(__x86_64__)
    #define GT64 1
#else
# error Unsupported architecture
#endif

typedef struct {
#ifdef GT64
    union {
        uint64_t rsp;
        uintptr_t stack;
    };
    uint64_t r15;
    uint64_t r14;
    uint64_t r13;
    uint64_t r12;
    uint64_t rbx;
    uint64_t rbp;
    uint64_t rdi;
    uint64_t rsi;
#else
    uint32_t ebx;
    uint32_t esi;
    uint32_t edi;
    union {
        uint32_t ebp;
        uintptr_t stack;
    };
    uint32_t esp;
#endif
} gt_regs_t;

struct __gt_thread {
    gt_regs_t regs;
    gt_thread_state_t state;
    uint8_t* stack;
    struct __gt_thread* caller;
    destructor_t* dtors;
    uint32_t ndtors;
};

struct __gt_ctx {
    gt_thread_t* current;
    gt_thread_t* root;
    void* buffer;
};

//abandon all hope, ye who enter here
extern void gt_do_switch(gt_thread_t* current, gt_thread_t* to);
extern void gt_die();
extern void gt_start_thread();

void gt_do_return(gt_ctx_t* ctx, gt_thread_t* thread) {
    (void)ctx;
    thread->state = gt_state_dead;
    ctx->buffer = NULL;
    gt_do_switch(thread, thread->caller);
}

void gt_thread_destroy(gt_thread_t* thread) {
    for(uint32_t i = 0; i < thread->ndtors; i++) {
        destructor_t d = thread->dtors[i];
        d.fn(d.arg);
    }
    free(thread->dtors);
    thread->dtors = NULL;
    thread->ndtors = 0;

    free(thread->stack);
    thread->stack = NULL;
}

gt_ctx_t* gt_ctx_create() {
    gt_ctx_t* ctx = malloc(sizeof(gt_ctx_t));
    ctx->current = ctx->root = malloc(sizeof(gt_thread_t));
    ctx->current->stack = NULL;
    ctx->current->dtors = NULL;
    ctx->current->ndtors = 0;
    return ctx;
}

void gt_ctx_free(gt_ctx_t* ctx) {
    gt_thread_free(ctx->root);
    free(ctx);
}

gt_thread_t* gt_thread_create(gt_ctx_t* ctx, gt_start_fn fn) {
    gt_thread_t* thread = malloc(sizeof(gt_thread_t));
    thread->dtors = NULL;
    thread->ndtors = 0;

    size_t stacksize = 131072;
    uint8_t* stack = malloc(sizeof(uint8_t) * stacksize);
    memset(&thread->regs, 0, sizeof(gt_regs_t));
    int pos = 0;
#define PUSH(x) do { *(PTR*)(&stack[stacksize - PTRSZ*(++pos)]) = (PTR)(x); } while(0)
    PUSH(ctx);
    PUSH(thread);
    PUSH(gt_die);
    PUSH(ctx);
    PUSH(fn);
    PUSH(gt_start_thread);
    thread->regs.stack = (PTR)&stack[stacksize - PTRSZ*pos];

#ifndef GT64
    thread->regs.esp = thread->regs.ebp;
#endif

    thread->stack = stack;
    thread->state = gt_state_new;
    return thread;
}

gt_thread_t* gt_thread_create_child(gt_ctx_t* ctx, gt_start_fn fn) {
    gt_thread_t* thread = gt_thread_create(ctx, fn);
    destructor_t dtor = {
        .fn = (void (*)(void*))gt_thread_free,
        .arg = thread
    };
    gt_register_destructor(ctx, dtor);
    return thread;
}

void gt_thread_free(gt_thread_t* thread) {
    gt_thread_destroy(thread);
    free(thread);
}

gt_thread_state_t gt_thread_state(gt_thread_t* thread) {
    return thread->state;
}

void* gt_thread_resume(gt_ctx_t* ctx, gt_thread_t* to, void* arg) {
    if(to->state == gt_state_dead) {
        return NULL;
    }
    gt_thread_t* curr = ctx->current;
    gt_thread_t* currcall = curr->caller;
    
    to->caller = curr;
    ctx->current = to;

    ctx->buffer = arg;

    gt_do_switch(curr, to);
    if(to->state == gt_state_dead) {
        gt_thread_destroy(to);
    }

    curr->caller = currcall;

    return ctx->buffer;
}

void gt_thread_switch(gt_thread_t* from, gt_thread_t* to) {
    gt_do_switch(from, to);
}

gt_thread_t* gt_caller(gt_ctx_t* ctx) {
    return ctx->current->caller;
}

gt_thread_t* gt_current(gt_ctx_t* ctx) {
    return ctx->current;
}

void gt_register_destructor(gt_ctx_t* ctx, destructor_t destructor) {
    gt_thread_t* t = ctx->current;
    destructor_t* d = realloc(t->dtors, t->ndtors + 1);
    d[t->ndtors] = destructor;
    t->ndtors++;
    t->dtors = d;
}

