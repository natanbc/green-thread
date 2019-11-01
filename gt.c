#include <stdlib.h>
#include <string.h>
#include <malloc.h>
#include "gt.h"

#define PTR   uintptr_t
#define PTRSZ sizeof(PTR)

#if defined(__i386__)
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

//this is just a marker type.
struct __gt_tls {};

struct __gt_thread {
    gt_regs_t regs;
    gt_thread_state_t state;
    uint8_t* stack;
    struct __gt_thread* caller;
    destructor_t* dtors;
    uint32_t ndtors;
    void** tls;
    uint32_t ntls;
};

struct __gt_ctx {
    gt_thread_t* current;
    gt_thread_t* root;
    void* buffer;
    size_t stacksize;
    uint32_t ntls;
};

//abandon all hope, ye who enter here
extern void gt_thread_switch(gt_thread_t* current, gt_thread_t* to);
extern void gt_die();
#ifdef GT64
extern void gt_start_thread();
#endif

void gt_do_return(gt_ctx_t* ctx, gt_thread_t* thread) {
    (void)ctx;
    thread->state = gt_state_dead;
    ctx->buffer = NULL;
    gt_thread_switch(thread, thread->caller);
}

void gt_thread_destroy(gt_thread_t* thread) {
    for(uint32_t i = 0; i < thread->ndtors; i++) {
        destructor_t d = thread->dtors[i];
        d.fn(d.arg);
    }
    free(thread->dtors);
    thread->dtors = NULL;
    thread->ndtors = 0;

    free(thread->tls);
    thread->tls = NULL;
    thread->ntls = 0;

    free(thread->stack);
    thread->stack = NULL;
}

gt_ctx_t* gt_ctx_create() {
    gt_ctx_t* ctx = malloc(sizeof(gt_ctx_t));
    ctx->stacksize = 131072;
    ctx->ntls = 0;
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

void gt_ctx_set_stack_size(gt_ctx_t* ctx, size_t size) {
    if(size >= 4096) {
        ctx->stacksize = size;
    }
}

#ifndef GT64
void gt_start_thread(gt_start_fn fn, gt_ctx_t* ctx) {
    ctx->current->state = gt_state_alive;
    fn(ctx, ctx->buffer);
}
#endif

gt_thread_t* gt_thread_create(gt_ctx_t* ctx, gt_start_fn fn) {
    gt_thread_t* thread = malloc(sizeof(gt_thread_t));
    thread->dtors = NULL;
    thread->ndtors = 0;

    size_t stacksize = ctx->stacksize;
    uint8_t* stack = memalign(64, sizeof(uint8_t) * stacksize);
    memset(&thread->regs, 0, sizeof(gt_regs_t));
    int pos = 0;
#define PUSH(x) do { *(PTR*)(&stack[stacksize - PTRSZ*(++pos)]) = (PTR)(x); } while(0)
#ifdef GT64
    PUSH(0); //align stack
    PUSH(ctx);
    PUSH(thread);
    PUSH(gt_die);
    PUSH(ctx);
    PUSH(fn);
    PUSH(gt_start_thread);
#else
    PUSH(ctx);
    PUSH(thread);
    PUSH(ctx);
    PUSH(fn);
    PUSH(gt_die);
    PUSH(gt_start_thread);
#endif
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

gt_tls_t* gt_tls_new(gt_ctx_t* ctx) {
    return (gt_tls_t*)(size_t)(++ctx->ntls);
}

inline void** gt_tls_get_location(gt_ctx_t* ctx, gt_tls_t* tls) {
    gt_thread_t* t = ctx->current;
    uint32_t n = (uint32_t)(size_t)tls;
    if(t->ntls < n) {
        t->tls = realloc(t->tls, n);
        t->ntls = n;
        t->tls[n-1] = NULL;
    }
    return &t->tls[n-1];
}

void* gt_tls_get(gt_ctx_t* ctx, gt_tls_t* tls) {
    return *gt_tls_get_location(ctx, tls);
}

void* gt_tls_set(gt_ctx_t* ctx, gt_tls_t* tls, void* value) {
    void** slot = gt_tls_get_location(ctx, tls);
    void* old = *slot;
    *slot = value;
    return old;
}

void gt_tls_free(gt_ctx_t* ctx, gt_tls_t* tls) {
    //nothing to do
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

    gt_thread_switch(curr, to);
    if(to->state == gt_state_dead) {
        gt_thread_destroy(to);
    }

    curr->caller = currcall;

    return ctx->buffer;
}

void* gt_thread_yield(gt_ctx_t* ctx, void* arg) {
    return gt_thread_resume(ctx, gt_caller(ctx), arg);
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

