#ifndef HALIDE_HALIDERUNTIME_H
#define HALIDE_HALIDERUNTIME_H

#ifndef COMPILING_HALIDE_RUNTIME
#include <stddef.h>
#include <stdint.h>
#else
#include "runtime_internal.h"
#endif

#ifdef __cplusplus
extern "C" {
#endif

/** \file
 *
 * This file exports all routines which can be replaced by an
 * application hosting code generated by %Halide. These are used when
 * doing Ahead Of Time (AOT) compilation and must be supplied to the
 * linker to override a routine. I.e., just define your own version of
 * any of these functions with extern "C" linkage, and it should
 * replace the default one.
 *
 * When doing Just In Time (JIT) compilation methods on the Func being
 * compiled must be called instead. The corresponding methods are
 * documented below.
 *
 * All of these functions take a "void *user_context" parameter as their
 * first argument; if the Halide kernel that calls back to any of these
 * functions has been compiled with the UserContext feature set on its Target,
 * then the value of that pointer passed from the code that calls the
 * Halide kernel is piped through to the function.
 *
 * Some of these are also useful to call when using the default
 * implementation. E.g. halide_shutdown_thread_pool.
 *
 * Note that some linker setups may not respect the override you
 * provide. E.g. if the override is in a shared library and the halide
 * object files are linked directly into the output, the builtin
 * versions of the runtime functions will be called. See your linker
 * documentation for more details. On Linux, LD_DYNAMIC_WEAK=1 may
 * help.
 *
 */

/** Print a message to stderr. Main use is to support HL_TRACE
 * functionality, print, and print_when calls. Also called by the default
 * halide_error.  This function can be replaced in JITed code by using
 * halide_custom_print and providing an implementation of halide_print
 * in AOT code. See Func::set_custom_print.
 */
extern void halide_print(void *user_context, const char *);

/** Define halide_error to catch errors messages at runtime (for
 * example bounds checking failures). This function can be replaced in
 * JITed code by using halide_set_error_handler and providing an
 * implementation of halide_error in AOT code. See
 * Func::set_error_handler.
 */
extern void halide_error(void *user_context, const char *);

/** A macro that calls halide_error if the supplied condition is false. */
#define halide_assert(user_context, cond) if (!(cond)) halide_error(user_context, #cond);

/** These are allocated statically inside the runtime, hence the fixed
 * size. They must be initialized with zero. The first time
 * halide_mutex_lock is called, the lock must be initialized in a
 * thread safe manner. This incurs a small overhead for a once
 * mechanism, but makes the lock reliably easy to setup and use
 * without depending on e.g. C++ constructor logic.
 */
struct halide_mutex {
    uint64_t _private[8];
};

/** A basic set of mutex functions, which call platform specific code
 * for mutual exclusion.
 */
//@{
extern void halide_mutex_lock(struct halide_mutex *mutex);
extern void halide_mutex_unlock(struct halide_mutex *mutex);
extern void halide_mutex_cleanup(struct halide_mutex *mutex_arg);
//@}

/** Define halide_do_par_for to replace the default thread pool
 * implementation. halide_shutdown_thread_pool can also be called to
 * release resources used by the default thread pool on platforms
 * where it makes sense. (E.g. On Mac OS, Grand Central Dispatch is
 * used so %Halide does not own the threads backing the pool and they
 * cannot be released.)  See Func::set_custom_do_task and
 * Func::set_custom_do_par_for. Should return zero if all the jobs
 * return zero, or an arbitrarily chosen return value from one of the
 * jobs otherwise.
 */
//@{
extern int halide_do_par_for(void *user_context,
                             int (*f)(void *ctx, int, uint8_t *),
                             int min, int size, uint8_t *closure);
extern void halide_shutdown_thread_pool();
//@}

/** Set the number of threads used by Halide's thread pool. No effect
 * on OS X or iOS. If changed after the first use of a parallel Halide
 * routine, shuts down and then reinitializes the thread pool. */
extern void halide_set_num_threads(int n);

/** Define halide_malloc and halide_free to replace the default memory
 * allocator.  See Func::set_custom_allocator. (Specifically note that
 * halide_malloc must return a 32-byte aligned pointer, and it must be
 * safe to read at least 8 bytes before the start and beyond the
 * end.)
 */
//@{
extern void *halide_malloc(void *user_context, size_t x);
extern void halide_free(void *user_context, void *ptr);
//@}

/** Called when debug_to_file is used inside %Halide code.  See
 * Func::debug_to_file for how this is called
 *
 * Cannot be replaced in JITted code at present.
 */
extern int32_t halide_debug_to_file(void *user_context, const char *filename,
                                    uint8_t *data, int32_t s0, int32_t s1, int32_t s2,
                                    int32_t s3, int32_t type_code,
                                    int32_t bytes_per_element);


enum halide_trace_event_code {halide_trace_load = 0,
                              halide_trace_store = 1,
                              halide_trace_begin_realization = 2,
                              halide_trace_end_realization = 3,
                              halide_trace_produce = 4,
                              halide_trace_update = 5,
                              halide_trace_consume = 6,
                              halide_trace_end_consume = 7};

#pragma pack(push, 1)
struct halide_trace_event {
    const char *func;
    halide_trace_event_code event;
    int32_t parent_id;
    int32_t type_code;
    int32_t bits;
    int32_t vector_width;
    int32_t value_index;
    void *value;
    int32_t dimensions;
    int32_t *coordinates;
};
#pragma pack(pop)

/** Called when Funcs are marked as trace_load, trace_store, or
 * trace_realization. See Func::set_custom_trace. The default
 * implementation either prints events via halide_printf, or if
 * HL_TRACE_FILE is defined, dumps the trace to that file in a
 * yet-to-be-documented binary format (see src/runtime/tracing.cpp to
 * reverse engineer the format). If the trace is going to be large,
 * you may want to make the file a named pipe, and then read from that
 * pipe into gzip.
 *
 * halide_trace returns a unique ID which will be passed to future
 * events that "belong" to the earlier event as the parent id. The
 * ownership hierarchy looks like:
 *
 * begin_realization
 *    produce
 *      store
 *      update
 *      load/store
 *      consume
 *      load
 *      end_consume
 *    end_realization
 *
 * Threading means that ownership cannot be inferred from the ordering
 * of events. There can be many active realizations of a given
 * function, or many active productions for a single
 * realization. Within a single production, the ordering of events is
 * meaningful.
 */
extern int32_t halide_trace(void *user_context, const halide_trace_event *event);

/** Set the file descriptor that Halide should write binary trace
 * events to. If called with 0 as the argument, Halide outputs trace
 * information to stdout in a human-readable format. If never called,
 * Halide checks the for existence of an environment variable called
 * HL_TRACE_FILE and opens that file. If HL_TRACE_FILE is not defined,
 * it outputs trace information to stdout in a human-readable
 * format. */
extern void halide_set_trace_file(int fd);

/** Halide calls this to retrieve the file descriptor to write binary
 * trace events to. The default implementation returns the value set
 * by halide_set_trace_file. Implement it yourself if you wish to use
 * a custom file descriptor per user_context. Return zero from your
 * implementation to tell Halide to print human-readable trace
 * information to stdout. */
extern int halide_get_trace_file(void *user_context);

/** If tracing is writing to a file. This call closes that file
 * (flushing the trace). Returns zero on success. */
extern int halide_shutdown_trace();

/** All Halide GPU or device backend implementations much provide an interface
 * to be used with halide_device_malloc, etc.
 */
struct halide_device_interface;

/** Release all data associated with the current GPU backend, in particular
 * all resources (memory, texture, context handles) allocated by Halide. Must
 * be called explicitly when using AOT compilation. */
extern void halide_device_release(void *user_context, const halide_device_interface *interface);

/** Copy image data from device memory to host memory. This must be called
 * explicitly to copy back the results of a GPU-based filter. */
extern int halide_copy_to_host(void *user_context, struct buffer_t *buf);

/** Copy image data from host memory to device memory. This should not
 * be called directly; Halide handles copying to the device
 * automatically.  If interface is NULL and the bug has a non-zero dev
 * field, the device associated with the dev handle will be
 * used. Otherwise if the dev field is 0 and interface is NULL, an
 * error is returned. */
extern int halide_copy_to_device(void *user_context, struct buffer_t *buf,
                                 const halide_device_interface *interface);

/** Wait for current GPU operations to complete. Calling this explicitly
 * should rarely be necessary, except maybe for profiling. */
extern int halide_device_sync(void *user_context, struct buffer_t *buf);

/** Allocate device memory to back a buffer_t. */
extern int halide_device_malloc(void *user_context, struct buffer_t *buf, const halide_device_interface *interface);

extern int halide_device_free(void *user_context, struct buffer_t *buf);

/** Selects which gpu device to use. 0 is usually the display
 * device. If never called, Halide uses the environment variable
 * HL_GPU_DEVICE. If that variable is unset, Halide uses the last
 * device. Set this to -1 to use the last device. */
extern void halide_set_gpu_device(int n);

/** Halide calls this to get the desired halide gpu device
 * setting. Implement this yourself to use a different gpu device per
 * user_context. The default implementation returns the value set by
 * halide_set_gpu_device, or the environment variable
 * HL_GPU_DEVICE. */
extern int halide_get_gpu_device(void *user_context);

/** Set the soft maximum amount of memory, in bytes, that the LRU
 *  cache will use to memoize Func results.  This is not a strict
 *  maximum in that concurrency and simultaneous use of memoized
 *  reults larger than the cache size can both cause it to
 *  temporariliy be larger than the size specified here.
 */
extern void halide_memoization_cache_set_size(int64_t size);

/** Given a cache key for a memoized result, currently constructed
 *  from the Func name and top-level Func name plus the arguments of
 *  the computation, determine if the result is in the cache and
 *  return it if so. (The internals of the cache key should be
 *  considered opaque by this function.) If this routine returns true,
 *  it is a cache miss. Otherwise, it will return false and the
 *  buffers passed in will be filled, via copying, with memoized
 *  data. The last argument is a list if buffer_t pointers which
 *  represents the outputs of the memoized Func. If the Func does not
 *  return a Tuple, there will only be one buffer_t in the list. The
 *  tuple_count parameters determines the length of the list.
 */
extern bool halide_memoization_cache_lookup(void *user_context, const uint8_t *cache_key, int32_t size,
                                            buffer_t *realized_bounds, int32_t tuple_count, buffer_t **tuple_buffers);

/** Given a cache key for a memoized result, currently constructed
 *  from the Func name and top-level Func name plus the arguments of
 *  the computation, store the result in the cache for futre access by
 *  halide_memoization_cache_lookup. (The internals of the cache key
 *  should be considered opaque by this function.) Data is copied out
 *  from the inputs and inputs are unmodified. The last argument is a
 *  list if buffer_t pointers which represents the outputs of the
 *  memoized Func. If the Func does not return a Tuple, there will
 *  only be one buffer_t in the list. The tuple_count parameters
 *  determines the length of the list.
 */
extern void halide_memoization_cache_store(void *user_context, const uint8_t *cache_key, int32_t size,
                                           buffer_t *realized_bounds, int32_t tuple_count, buffer_t **tuple_buffers);

/** Free all memory and resources associated with the memoization cache.
 * Must be called at a time when no other threads are accessing the cache.
 */
extern void halide_memoization_cache_cleanup();

/** Types in the halide type system. They can be ints, unsigned ints,
 * or floats (of various bit-widths), or a handle (which is always pointer-sized).
 * Note that the int/uint/float values do not imply a specific bit width
 * (the bit width is expected to be encoded in a separate value).
 */
typedef enum halide_type_code_t {
    halide_type_int = 0,   //!< signed integers
    halide_type_uint = 1,  //!< unsigned integers
    halide_type_float = 2, //!< floating point numbers
    halide_type_handle = 3 //!< opaque pointer type (void *)
} halide_type_code_t;

#ifndef BUFFER_T_DEFINED
#define BUFFER_T_DEFINED

/**
 * The raw representation of an image passed around by generated
 * Halide code. It includes some stuff to track whether the image is
 * not actually in main memory, but instead on a device (like a
 * GPU). */
#pragma pack(push, 1)
typedef struct buffer_t {
  /** A device-handle for e.g. GPU memory used to back this buffer. */
  uint64_t dev;

  /** A pointer to the start of the data in main memory. */
  uint8_t* host;

  /** The size of the buffer in each dimension. */
  int32_t extent[4];

  /** Gives the spacing in memory between adjacent elements in the
   * given dimension.  The correct memory address for a load from
   * this buffer at position x, y, z, w is:
   * host + (x * stride[0] + y * stride[1] + z * stride[2] + w * stride[3]) * elem_size
   * By manipulating the strides and extents you can lazily crop,
   * transpose, and even flip buffers without modifying the data.
   */
  int32_t stride[4];

  /** Buffers often represent evaluation of a Func over some
   * domain. The min field encodes the top left corner of the
   * domain. */
  int32_t min[4];

  /** How many bytes does each buffer element take. This may be
   * replaced with a more general type code in the future. */
  int32_t elem_size;

  /** This should be true if there is an existing device allocation
   * mirroring this buffer, and the data has been modified on the
   * host side. */
  bool host_dirty;

  /** This should be true if there is an existing device allocation
   mirroring this buffer, and the data has been modified on the
   device side. */
  bool dev_dirty;

  uint8_t _padding[2];
} buffer_t;
#pragma pack(pop)

#endif

/** halide_scalar_value_t is a simple union able to represent all the well-known
 * scalar values in a filter argument. Note that it isn't tagged with a type;
 * you must ensure you know the proper type before accessing. Most user
 * code will never need to create instances of this struct; its primary use
 * is to hold def/min/max values in a halide_filter_argument_t. */
union halide_scalar_value_t {
  bool b;
  int8_t i8;
  int16_t i16;
  int32_t i32;
  int64_t i64;
  uint8_t u8;
  uint16_t u16;
  uint32_t u32;
  uint64_t u64;
  float f32;
  double f64;
  void *handle;
};

enum halide_argument_kind_t {
    HalideArgumentKind_InputScalar = 0,
    HalideArgumentKind_InputBuffer = 1,
    HalideArgumentKind_OutputBuffer = 2
};

// Some compile environments can add padding to the end of the struct
// without #pragma pack, which will mess up the LLVM initialization code.

/**
 * halide_filter_argument_t is essentially a plain-C-struct equivalent to
 * Halide::Argument; most user code will never need to create one.
 */
#pragma pack(push, 1)
struct halide_filter_argument_t {
  const char *name;       // name of the argument; will never be null or empty.
  // TODO: all these would fit into uint8. Should we pack them tightly?
  int32_t kind;           // actually halide_argument_kind_t
  int32_t dimensions;     // always zero for scalar arguments
  int32_t type_code;      // actually halide_type_code_t
  int32_t type_bits;      // [1, 8, 16, 32, 64]
  // These pointers should always be null for buffer arguments,
  // and *may* be null for scalar arguments. (A null value means )
  const halide_scalar_value_t *def;
  const halide_scalar_value_t *min;
  const halide_scalar_value_t *max;
};
#pragma pack(pop)

#pragma pack(push, 1)
struct halide_filter_metadata_t {
  // TODO: should we add a version field at the beginning, in case we want
  // to expand metadata later? (Or would we be better off simply adding more
  // external symbols in the future?)

  /** The Target for which the filter was compiled. This is always
   * a canonical Target string (ie a product of Target::to_string). */
  const char* target;

  /** An array of the filters input and output arguments; this will never be
   * null. The order of arguments is not guaranteed (input and output arguments
   * may come in any order); however, it is guaranteed that all arguments
   * will have a unique name within a given filter. */
  const halide_filter_argument_t* arguments;

  /** The number of entries in the arguments field. This is always >= 1. */
  int32_t num_arguments;
};
#pragma pack(pop)

#ifdef __cplusplus
} // End extern "C"
#endif

#endif // HALIDE_HALIDERUNTIME_H
