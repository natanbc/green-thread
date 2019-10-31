#include <stdint.h>

typedef struct {
    void (*fn)(void*);
    void* arg;
} destructor_t;

typedef enum {
    gt_state_new = 0,
    gt_state_alive = 1,
    gt_state_dead = 2
} gt_thread_state_t;

typedef struct __gt_thread gt_thread_t;
typedef struct __gt_ctx gt_ctx_t;

typedef void (*gt_start_fn)(gt_ctx_t*, void*);

/**
 * Creates a new gt context. This context can only be shared across gt threads.
 *
 * @return The new context.
 */
gt_ctx_t* gt_ctx_create();

/**
 * Frees the given context.
 */
void gt_ctx_free(gt_ctx_t* ctx);

/**
 * Creates a new thread that will run the given function. A stack of 128KiB is
 * allocated for the thread.
 *
 * @param ctx Context to create this thread in.
 * @param fn Function to run when the thread starts.
 *
 * @return The new thread.
 */
gt_thread_t* gt_thread_create(gt_ctx_t* ctx, gt_start_fn fn);

/**
 * Creates a new thread that gets destroyed when the current thread is destroyed.
 * This function is equivalent to
 * ```
 * gt_thread_t* thread = gt_thread_create(ctx, fn);
 * destructor_t dtor = {
 *     .fn = (void(*)(void*))gt_thread_free,
 *     .arg = thread
 * };
 * gt_register_destructor(ctx, dtor);
 * return thread;
 * ```
 *
 * @param ctx Context to create this thread in.
 * @param fn Function to run when the thread starts.
 *
 * @return The new thread.
 */
gt_thread_t* gt_thread_create_child(gt_ctx_t* ctx, gt_start_fn fn);

/**
 * Frees a thread, deallocating it's stack and the thread struct. Any destructors
 * registered are executed before the stack is deallocated.
 *
 * @param thread Thread to deallocate.
 *
 * @see gt_register_destructor
 */
void gt_thread_free(gt_thread_t* thread);

/**
 * @return The state of the thread.
 */
gt_thread_state_t gt_thread_state(gt_thread_t* thread);

/**
 * Resumes a thread, passing an argument to it. If the thread has not been
 * started yet, the argument is passed as the second argument to it's function,
 * otherwise it's returned by the gt_thread_resume call done by the destination
 * thread.
 *
 * The current thread is suspended until another thread calls gt_thread_resume on
 * it, returning the value passed as `arg` to resume.
 *
 * Resuming the current thread is a noop.
 *
 * @param ctx Context to run this operation on.
 * @param to Thread to resume.
 * @param arg Argument to pass to the thread.
 */
void* gt_thread_resume(gt_ctx_t* ctx, gt_thread_t* to, void* arg);

/**
 * Switches the currently running thread with another one. This is a low
 * level operation and should normally not be used.
 *
 * Switching from the current thread to the current thread is a noop.
 *
 * The `from` argument *must* be the current thread.
 *
 * @param from Current thread.
 * @param to Thread to switch to.
 *
 * @see gt_current
 * @see gt_thread_resume
 */
void gt_thread_switch(gt_thread_t* from, gt_thread_t* to);

/**
 * @return The caller of this thread. May be null.
 */
gt_thread_t* gt_caller(gt_ctx_t* ctx);

/**
 * @return The current thread. Never null.
 */
gt_thread_t* gt_current(gt_ctx_t* ctx);

/**
 * Registers a destructor to run when the current thread dies. The destructors
 * are guaranteed to run before the stack is deallocated, so it's safe to pass
 * pointers to stack values as the `arg` field of the destructor_t struct.
 *
 * @param ctx Context to register this destructor on.
 * @param dtor Destructor to register.
 */
void gt_register_destructor(gt_ctx_t* ctx, destructor_t dtor);

