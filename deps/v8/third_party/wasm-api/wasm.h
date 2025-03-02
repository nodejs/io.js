// WebAssembly C API

#ifndef WASM_H
#define WASM_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <assert.h>

#ifndef WASM_API_EXTERN
#if defined(_WIN32) && !defined(__MINGW32__) && !defined(LIBWASM_STATIC)
#define WASM_API_EXTERN __declspec(dllimport)
#else
#define WASM_API_EXTERN
#endif
#endif

#ifdef __cplusplus
extern "C" {
#endif

///////////////////////////////////////////////////////////////////////////////
// Auxiliaries

// Machine types

inline void assertions(void) {
  static_assert(sizeof(float) == sizeof(uint32_t), "incompatible float type");
  static_assert(sizeof(double) == sizeof(uint64_t), "incompatible double type");
  static_assert(sizeof(intptr_t) == sizeof(uint32_t) ||
                sizeof(intptr_t) == sizeof(uint64_t),
                "incompatible pointer type");
}

typedef char byte_t;
typedef float float32_t;
typedef double float64_t;


// Ownership

#define own

// The qualifier `own` is used to indicate ownership of data in this API.
// It is intended to be interpreted similar to a `const` qualifier:
//
// - `own wasm_xxx_t*` owns the pointed-to data
// - `own wasm_xxx_t` distributes to all fields of a struct or union `xxx`
// - `own wasm_xxx_vec_t` owns the vector as well as its elements(!)
// - an `own` function parameter passes ownership from caller to callee
// - an `own` function result passes ownership from callee to caller
// - an exception are `own` pointer parameters named `out`, which are copy-back
//   output parameters passing back ownership from callee to caller
//
// Own data is created by `wasm_xxx_new` functions and some others.
// It must be released with the corresponding `wasm_xxx_delete` function.
//
// Deleting a reference does not necessarily delete the underlying object,
// it merely indicates that this owner no longer uses it.
//
// For vectors, `const wasm_xxx_vec_t` is used informally to indicate that
// neither the vector nor its elements should be modified.
// TODO: introduce proper `wasm_xxx_const_vec_t`?


#define WASM_DECLARE_OWN(name) \
  typedef struct wasm_##name##_t wasm_##name##_t; \
  \
  WASM_API_EXTERN void wasm_##name##_delete(own wasm_##name##_t*);


// Vectors

#define WASM_DECLARE_VEC(name, ptr_or_none) \
  typedef struct wasm_##name##_vec_t { \
    size_t size; \
    wasm_##name##_t ptr_or_none* data; \
  } wasm_##name##_vec_t; \
  \
  WASM_API_EXTERN void wasm_##name##_vec_new_empty(own wasm_##name##_vec_t* out); \
  WASM_API_EXTERN void wasm_##name##_vec_new_uninitialized( \
    own wasm_##name##_vec_t* out, size_t); \
  WASM_API_EXTERN void wasm_##name##_vec_new( \
    own wasm_##name##_vec_t* out, \
    size_t, own wasm_##name##_t ptr_or_none const[]); \
  WASM_API_EXTERN void wasm_##name##_vec_copy( \
    own wasm_##name##_vec_t* out, const wasm_##name##_vec_t*); \
  WASM_API_EXTERN void wasm_##name##_vec_delete(own wasm_##name##_vec_t*);


// Byte vectors

typedef byte_t wasm_byte_t;
WASM_DECLARE_VEC(byte, )

typedef wasm_byte_vec_t wasm_name_t;

#define wasm_name wasm_byte_vec
#define wasm_name_new wasm_byte_vec_new
#define wasm_name_new_empty wasm_byte_vec_new_empty
#define wasm_name_new_new_uninitialized wasm_byte_vec_new_uninitialized
#define wasm_name_copy wasm_byte_vec_copy
#define wasm_name_delete wasm_byte_vec_delete

static inline void wasm_name_new_from_string(
  own wasm_name_t* out, const char* s
) {
  wasm_name_new(out, strlen(s), s);
}

static inline void wasm_name_new_from_string_nt(
  own wasm_name_t* out, const char* s
) {
  wasm_name_new(out, strlen(s) + 1, s);
}


///////////////////////////////////////////////////////////////////////////////
// Runtime Environment

// Configuration

WASM_DECLARE_OWN(config)

WASM_API_EXTERN own wasm_config_t* wasm_config_new(void);

// Embedders may provide custom functions for manipulating configs.


// Engine

WASM_DECLARE_OWN(engine)

WASM_API_EXTERN own wasm_engine_t* wasm_engine_new(void);
WASM_API_EXTERN own wasm_engine_t* wasm_engine_new_with_config(own wasm_config_t*);


// Store

WASM_DECLARE_OWN(store)

WASM_API_EXTERN own wasm_store_t* wasm_store_new(wasm_engine_t*);


///////////////////////////////////////////////////////////////////////////////
// Type Representations

// Type attributes

typedef uint8_t wasm_mutability_t;
enum wasm_mutability_enum {
  WASM_CONST,
  WASM_VAR,
};

typedef struct wasm_limits_t {
  uint32_t min;
  uint32_t max;
} wasm_limits_t;

static const uint32_t wasm_limits_max_default = 0xffffffff;


// Generic

#define WASM_DECLARE_TYPE(name) \
  WASM_DECLARE_OWN(name) \
  WASM_DECLARE_VEC(name, *) \
  \
  WASM_API_EXTERN own wasm_##name##_t* wasm_##name##_copy(const wasm_##name##_t*);


// Value Types

WASM_DECLARE_TYPE(valtype)

typedef uint8_t wasm_valkind_t;
enum wasm_valkind_enum {
  WASM_I32,
  WASM_I64,
  WASM_F32,
  WASM_F64,
  WASM_EXTERNREF = 128,
  WASM_FUNCREF,
};

WASM_API_EXTERN own wasm_valtype_t* wasm_valtype_new(wasm_valkind_t);

WASM_API_EXTERN wasm_valkind_t wasm_valtype_kind(const wasm_valtype_t*);

static inline bool wasm_valkind_is_num(wasm_valkind_t k) {
  return k < WASM_EXTERNREF;
}
static inline bool wasm_valkind_is_ref(wasm_valkind_t k) {
  return k >= WASM_EXTERNREF;
}

static inline bool wasm_valtype_is_num(const wasm_valtype_t* t) {
  return wasm_valkind_is_num(wasm_valtype_kind(t));
}
static inline bool wasm_valtype_is_ref(const wasm_valtype_t* t) {
  return wasm_valkind_is_ref(wasm_valtype_kind(t));
}


// Function Types

WASM_DECLARE_TYPE(functype)

WASM_API_EXTERN own wasm_functype_t* wasm_functype_new(
  own wasm_valtype_vec_t* params, own wasm_valtype_vec_t* results);

WASM_API_EXTERN const wasm_valtype_vec_t* wasm_functype_params(const wasm_functype_t*);
WASM_API_EXTERN const wasm_valtype_vec_t* wasm_functype_results(const wasm_functype_t*);


// Global Types

WASM_DECLARE_TYPE(globaltype)

WASM_API_EXTERN own wasm_globaltype_t* wasm_globaltype_new(
  own wasm_valtype_t*, wasm_mutability_t);

WASM_API_EXTERN const wasm_valtype_t* wasm_globaltype_content(const wasm_globaltype_t*);
WASM_API_EXTERN wasm_mutability_t wasm_globaltype_mutability(const wasm_globaltype_t*);


// Table Types

WASM_DECLARE_TYPE(tabletype)

WASM_API_EXTERN own wasm_tabletype_t* wasm_tabletype_new(
  own wasm_valtype_t*, const wasm_limits_t*);

WASM_API_EXTERN const wasm_valtype_t* wasm_tabletype_element(const wasm_tabletype_t*);
WASM_API_EXTERN const wasm_limits_t* wasm_tabletype_limits(const wasm_tabletype_t*);


// Memory Types

WASM_DECLARE_TYPE(memorytype)

WASM_API_EXTERN own wasm_memorytype_t* wasm_memorytype_new(const wasm_limits_t*);

WASM_API_EXTERN const wasm_limits_t* wasm_memorytype_limits(const wasm_memorytype_t*);


// Extern Types

WASM_DECLARE_TYPE(externtype)

typedef uint8_t wasm_externkind_t;
enum wasm_externkind_enum {
  WASM_EXTERN_FUNC,
  WASM_EXTERN_GLOBAL,
  WASM_EXTERN_TABLE,
  WASM_EXTERN_MEMORY,
};

WASM_API_EXTERN wasm_externkind_t wasm_externtype_kind(const wasm_externtype_t*);

WASM_API_EXTERN wasm_externtype_t* wasm_functype_as_externtype(wasm_functype_t*);
WASM_API_EXTERN wasm_externtype_t* wasm_globaltype_as_externtype(wasm_globaltype_t*);
WASM_API_EXTERN wasm_externtype_t* wasm_tabletype_as_externtype(wasm_tabletype_t*);
WASM_API_EXTERN wasm_externtype_t* wasm_memorytype_as_externtype(wasm_memorytype_t*);

WASM_API_EXTERN wasm_functype_t* wasm_externtype_as_functype(wasm_externtype_t*);
WASM_API_EXTERN wasm_globaltype_t* wasm_externtype_as_globaltype(wasm_externtype_t*);
WASM_API_EXTERN wasm_tabletype_t* wasm_externtype_as_tabletype(wasm_externtype_t*);
WASM_API_EXTERN wasm_memorytype_t* wasm_externtype_as_memorytype(wasm_externtype_t*);

WASM_API_EXTERN const wasm_externtype_t* wasm_functype_as_externtype_const(const wasm_functype_t*);
WASM_API_EXTERN const wasm_externtype_t* wasm_globaltype_as_externtype_const(const wasm_globaltype_t*);
WASM_API_EXTERN const wasm_externtype_t* wasm_tabletype_as_externtype_const(const wasm_tabletype_t*);
WASM_API_EXTERN const wasm_externtype_t* wasm_memorytype_as_externtype_const(const wasm_memorytype_t*);

WASM_API_EXTERN const wasm_functype_t* wasm_externtype_as_functype_const(const wasm_externtype_t*);
WASM_API_EXTERN const wasm_globaltype_t* wasm_externtype_as_globaltype_const(const wasm_externtype_t*);
WASM_API_EXTERN const wasm_tabletype_t* wasm_externtype_as_tabletype_const(const wasm_externtype_t*);
WASM_API_EXTERN const wasm_memorytype_t* wasm_externtype_as_memorytype_const(const wasm_externtype_t*);


// Import Types

WASM_DECLARE_TYPE(importtype)

WASM_API_EXTERN own wasm_importtype_t* wasm_importtype_new(
  own wasm_name_t* module, own wasm_name_t* name, own wasm_externtype_t*);

WASM_API_EXTERN const wasm_name_t* wasm_importtype_module(const wasm_importtype_t*);
WASM_API_EXTERN const wasm_name_t* wasm_importtype_name(const wasm_importtype_t*);
WASM_API_EXTERN const wasm_externtype_t* wasm_importtype_type(const wasm_importtype_t*);


// Export Types

WASM_DECLARE_TYPE(exporttype)

WASM_API_EXTERN own wasm_exporttype_t* wasm_exporttype_new(
  own wasm_name_t*, own wasm_externtype_t*);

WASM_API_EXTERN const wasm_name_t* wasm_exporttype_name(const wasm_exporttype_t*);
WASM_API_EXTERN const wasm_externtype_t* wasm_exporttype_type(const wasm_exporttype_t*);


///////////////////////////////////////////////////////////////////////////////
// Runtime Objects

// Values

struct wasm_ref_t;

typedef struct wasm_val_t {
  wasm_valkind_t kind;
  union {
    int32_t i32;
    int64_t i64;
    float32_t f32;
    float64_t f64;
    struct wasm_ref_t* ref;
  } of;
} wasm_val_t;

WASM_API_EXTERN void wasm_val_delete(own wasm_val_t* v);
WASM_API_EXTERN void wasm_val_copy(own wasm_val_t* out, const wasm_val_t*);

WASM_DECLARE_VEC(val, )


// References

#define WASM_DECLARE_REF_BASE(name) \
  WASM_DECLARE_OWN(name) \
  \
  WASM_API_EXTERN own wasm_##name##_t* wasm_##name##_copy(const wasm_##name##_t*); \
  WASM_API_EXTERN bool wasm_##name##_same(const wasm_##name##_t*, const wasm_##name##_t*); \
  \
  WASM_API_EXTERN void* wasm_##name##_get_host_info(const wasm_##name##_t*); \
  WASM_API_EXTERN void wasm_##name##_set_host_info(wasm_##name##_t*, void*); \
  WASM_API_EXTERN void wasm_##name##_set_host_info_with_finalizer( \
    wasm_##name##_t*, void*, void (*)(void*));

#define WASM_DECLARE_REF(name) \
  WASM_DECLARE_REF_BASE(name) \
  \
  WASM_API_EXTERN wasm_ref_t* wasm_##name##_as_ref(wasm_##name##_t*); \
  WASM_API_EXTERN wasm_##name##_t* wasm_ref_as_##name(wasm_ref_t*); \
  WASM_API_EXTERN const wasm_ref_t* wasm_##name##_as_ref_const(const wasm_##name##_t*); \
  WASM_API_EXTERN const wasm_##name##_t* wasm_ref_as_##name##_const(const wasm_ref_t*);

#define WASM_DECLARE_SHARABLE_REF(name) \
  WASM_DECLARE_REF(name) \
  WASM_DECLARE_OWN(shared_##name) \
  \
  WASM_API_EXTERN own wasm_shared_##name##_t* wasm_##name##_share(const wasm_##name##_t*); \
  WASM_API_EXTERN own wasm_##name##_t* wasm_##name##_obtain(wasm_store_t*, const wasm_shared_##name##_t*);


WASM_DECLARE_REF_BASE(ref)


// Frames

WASM_DECLARE_OWN(frame)
WASM_DECLARE_VEC(frame, *)
WASM_API_EXTERN own wasm_frame_t* wasm_frame_copy(const wasm_frame_t*);

WASM_API_EXTERN struct wasm_instance_t* wasm_frame_instance(const wasm_frame_t*);
WASM_API_EXTERN uint32_t wasm_frame_func_index(const wasm_frame_t*);
WASM_API_EXTERN size_t wasm_frame_func_offset(const wasm_frame_t*);
WASM_API_EXTERN size_t wasm_frame_module_offset(const wasm_frame_t*);


// Traps

typedef wasm_name_t wasm_message_t;  // null terminated

WASM_DECLARE_REF(trap)

WASM_API_EXTERN own wasm_trap_t* wasm_trap_new(wasm_store_t* store, const wasm_message_t*);

WASM_API_EXTERN void wasm_trap_message(const wasm_trap_t*, own wasm_message_t* out);
WASM_API_EXTERN own wasm_frame_t* wasm_trap_origin(const wasm_trap_t*);
WASM_API_EXTERN void wasm_trap_trace(const wasm_trap_t*, own wasm_frame_vec_t* out);


// Foreign Objects

WASM_DECLARE_REF(foreign)

WASM_API_EXTERN own wasm_foreign_t* wasm_foreign_new(wasm_store_t*);


// Modules

WASM_DECLARE_SHARABLE_REF(module)

WASM_API_EXTERN own wasm_module_t* wasm_module_new(
  wasm_store_t*, const wasm_byte_vec_t* binary);

WASM_API_EXTERN bool wasm_module_validate(wasm_store_t*, const wasm_byte_vec_t* binary);

WASM_API_EXTERN void wasm_module_imports(const wasm_module_t*, own wasm_importtype_vec_t* out);
WASM_API_EXTERN void wasm_module_exports(const wasm_module_t*, own wasm_exporttype_vec_t* out);

WASM_API_EXTERN void wasm_module_serialize(const wasm_module_t*, own wasm_byte_vec_t* out);
WASM_API_EXTERN own wasm_module_t* wasm_module_deserialize(wasm_store_t*, const wasm_byte_vec_t*);


// Function Instances

WASM_DECLARE_REF(func)

typedef own wasm_trap_t* (*wasm_func_callback_t)(
  const wasm_val_vec_t* args, own wasm_val_vec_t* results);
typedef own wasm_trap_t* (*wasm_func_callback_with_env_t)(
  void* env, const wasm_val_vec_t* args, wasm_val_vec_t* results);

WASM_API_EXTERN own wasm_func_t* wasm_func_new(
  wasm_store_t*, const wasm_functype_t*, wasm_func_callback_t);
WASM_API_EXTERN own wasm_func_t* wasm_func_new_with_env(
  wasm_store_t*, const wasm_functype_t* type, wasm_func_callback_with_env_t,
  void* env, void (*finalizer)(void*));

WASM_API_EXTERN own wasm_functype_t* wasm_func_type(const wasm_func_t*);
WASM_API_EXTERN size_t wasm_func_param_arity(const wasm_func_t*);
WASM_API_EXTERN size_t wasm_func_result_arity(const wasm_func_t*);

WASM_API_EXTERN own wasm_trap_t* wasm_func_call(
  const wasm_func_t*, const wasm_val_vec_t* args, wasm_val_vec_t* results);


// Global Instances

WASM_DECLARE_REF(global)

WASM_API_EXTERN own wasm_global_t* wasm_global_new(
  wasm_store_t*, const wasm_globaltype_t*, const wasm_val_t*);

WASM_API_EXTERN own wasm_globaltype_t* wasm_global_type(const wasm_global_t*);

WASM_API_EXTERN void wasm_global_get(const wasm_global_t*, own wasm_val_t* out);
WASM_API_EXTERN void wasm_global_set(wasm_global_t*, const wasm_val_t*);


// Table Instances

WASM_DECLARE_REF(table)

typedef uint32_t wasm_table_size_t;

WASM_API_EXTERN own wasm_table_t* wasm_table_new(
  wasm_store_t*, const wasm_tabletype_t*, wasm_ref_t* init);

WASM_API_EXTERN own wasm_tabletype_t* wasm_table_type(const wasm_table_t*);

WASM_API_EXTERN own wasm_ref_t* wasm_table_get(const wasm_table_t*, wasm_table_size_t index);
WASM_API_EXTERN bool wasm_table_set(wasm_table_t*, wasm_table_size_t index, wasm_ref_t*);

WASM_API_EXTERN wasm_table_size_t wasm_table_size(const wasm_table_t*);
WASM_API_EXTERN bool wasm_table_grow(wasm_table_t*, wasm_table_size_t delta, wasm_ref_t* init);


// Memory Instances

WASM_DECLARE_REF(memory)

typedef uint32_t wasm_memory_pages_t;

static const size_t MEMORY_PAGE_SIZE = 0x10000;

WASM_API_EXTERN own wasm_memory_t* wasm_memory_new(wasm_store_t*, const wasm_memorytype_t*);

WASM_API_EXTERN own wasm_memorytype_t* wasm_memory_type(const wasm_memory_t*);

WASM_API_EXTERN byte_t* wasm_memory_data(wasm_memory_t*);
WASM_API_EXTERN size_t wasm_memory_data_size(const wasm_memory_t*);

WASM_API_EXTERN wasm_memory_pages_t wasm_memory_size(const wasm_memory_t*);
WASM_API_EXTERN bool wasm_memory_grow(wasm_memory_t*, wasm_memory_pages_t delta);


// Externals

WASM_DECLARE_REF(extern)
WASM_DECLARE_VEC(extern, *)

WASM_API_EXTERN wasm_externkind_t wasm_extern_kind(const wasm_extern_t*);
WASM_API_EXTERN own wasm_externtype_t* wasm_extern_type(const wasm_extern_t*);

WASM_API_EXTERN wasm_extern_t* wasm_func_as_extern(wasm_func_t*);
WASM_API_EXTERN wasm_extern_t* wasm_global_as_extern(wasm_global_t*);
WASM_API_EXTERN wasm_extern_t* wasm_table_as_extern(wasm_table_t*);
WASM_API_EXTERN wasm_extern_t* wasm_memory_as_extern(wasm_memory_t*);

WASM_API_EXTERN wasm_func_t* wasm_extern_as_func(wasm_extern_t*);
WASM_API_EXTERN wasm_global_t* wasm_extern_as_global(wasm_extern_t*);
WASM_API_EXTERN wasm_table_t* wasm_extern_as_table(wasm_extern_t*);
WASM_API_EXTERN wasm_memory_t* wasm_extern_as_memory(wasm_extern_t*);

WASM_API_EXTERN const wasm_extern_t* wasm_func_as_extern_const(const wasm_func_t*);
WASM_API_EXTERN const wasm_extern_t* wasm_global_as_extern_const(const wasm_global_t*);
WASM_API_EXTERN const wasm_extern_t* wasm_table_as_extern_const(const wasm_table_t*);
WASM_API_EXTERN const wasm_extern_t* wasm_memory_as_extern_const(const wasm_memory_t*);

WASM_API_EXTERN const wasm_func_t* wasm_extern_as_func_const(const wasm_extern_t*);
WASM_API_EXTERN const wasm_global_t* wasm_extern_as_global_const(const wasm_extern_t*);
WASM_API_EXTERN const wasm_table_t* wasm_extern_as_table_const(const wasm_extern_t*);
WASM_API_EXTERN const wasm_memory_t* wasm_extern_as_memory_const(const wasm_extern_t*);


// Module Instances

WASM_DECLARE_REF(instance)

WASM_API_EXTERN own wasm_instance_t* wasm_instance_new(
  wasm_store_t*, const wasm_module_t*, const wasm_extern_vec_t* imports,
  own wasm_trap_t**
);

WASM_API_EXTERN void wasm_instance_exports(const wasm_instance_t*, own wasm_extern_vec_t* out);


///////////////////////////////////////////////////////////////////////////////
// Convenience

// Vectors

#define WASM_EMPTY_VEC {0, NULL}
#define WASM_ARRAY_VEC(array) {sizeof(array)/sizeof(*(array)), array}


// Value Type construction short-hands

static inline own wasm_valtype_t* wasm_valtype_new_i32(void) {
  return wasm_valtype_new(WASM_I32);
}
static inline own wasm_valtype_t* wasm_valtype_new_i64(void) {
  return wasm_valtype_new(WASM_I64);
}
static inline own wasm_valtype_t* wasm_valtype_new_f32(void) {
  return wasm_valtype_new(WASM_F32);
}
static inline own wasm_valtype_t* wasm_valtype_new_f64(void) {
  return wasm_valtype_new(WASM_F64);
}

static inline own wasm_valtype_t* wasm_valtype_new_externref(void) {
  return wasm_valtype_new(WASM_EXTERNREF);
}
static inline own wasm_valtype_t* wasm_valtype_new_funcref(void) {
  return wasm_valtype_new(WASM_FUNCREF);
}


// Function Types construction short-hands

static inline own wasm_functype_t* wasm_functype_new_0_0(void) {
  wasm_valtype_vec_t params, results;
  wasm_valtype_vec_new_empty(&params);
  wasm_valtype_vec_new_empty(&results);
  return wasm_functype_new(&params, &results);
}

static inline own wasm_functype_t* wasm_functype_new_1_0(
  own wasm_valtype_t* p
) {
  wasm_valtype_t* ps[1] = {p};
  wasm_valtype_vec_t params, results;
  wasm_valtype_vec_new(&params, 1, ps);
  wasm_valtype_vec_new_empty(&results);
  return wasm_functype_new(&params, &results);
}

static inline own wasm_functype_t* wasm_functype_new_2_0(
  own wasm_valtype_t* p1, own wasm_valtype_t* p2
) {
  wasm_valtype_t* ps[2] = {p1, p2};
  wasm_valtype_vec_t params, results;
  wasm_valtype_vec_new(&params, 2, ps);
  wasm_valtype_vec_new_empty(&results);
  return wasm_functype_new(&params, &results);
}

static inline own wasm_functype_t* wasm_functype_new_3_0(
  own wasm_valtype_t* p1, own wasm_valtype_t* p2, own wasm_valtype_t* p3
) {
  wasm_valtype_t* ps[3] = {p1, p2, p3};
  wasm_valtype_vec_t params, results;
  wasm_valtype_vec_new(&params, 3, ps);
  wasm_valtype_vec_new_empty(&results);
  return wasm_functype_new(&params, &results);
}

static inline own wasm_functype_t* wasm_functype_new_0_1(
  own wasm_valtype_t* r
) {
  wasm_valtype_t* rs[1] = {r};
  wasm_valtype_vec_t params, results;
  wasm_valtype_vec_new_empty(&params);
  wasm_valtype_vec_new(&results, 1, rs);
  return wasm_functype_new(&params, &results);
}

static inline own wasm_functype_t* wasm_functype_new_1_1(
  own wasm_valtype_t* p, own wasm_valtype_t* r
) {
  wasm_valtype_t* ps[1] = {p};
  wasm_valtype_t* rs[1] = {r};
  wasm_valtype_vec_t params, results;
  wasm_valtype_vec_new(&params, 1, ps);
  wasm_valtype_vec_new(&results, 1, rs);
  return wasm_functype_new(&params, &results);
}

static inline own wasm_functype_t* wasm_functype_new_2_1(
  own wasm_valtype_t* p1, own wasm_valtype_t* p2, own wasm_valtype_t* r
) {
  wasm_valtype_t* ps[2] = {p1, p2};
  wasm_valtype_t* rs[1] = {r};
  wasm_valtype_vec_t params, results;
  wasm_valtype_vec_new(&params, 2, ps);
  wasm_valtype_vec_new(&results, 1, rs);
  return wasm_functype_new(&params, &results);
}

static inline own wasm_functype_t* wasm_functype_new_3_1(
  own wasm_valtype_t* p1, own wasm_valtype_t* p2, own wasm_valtype_t* p3,
  own wasm_valtype_t* r
) {
  wasm_valtype_t* ps[3] = {p1, p2, p3};
  wasm_valtype_t* rs[1] = {r};
  wasm_valtype_vec_t params, results;
  wasm_valtype_vec_new(&params, 3, ps);
  wasm_valtype_vec_new(&results, 1, rs);
  return wasm_functype_new(&params, &results);
}

static inline own wasm_functype_t* wasm_functype_new_0_2(
  own wasm_valtype_t* r1, own wasm_valtype_t* r2
) {
  wasm_valtype_t* rs[2] = {r1, r2};
  wasm_valtype_vec_t params, results;
  wasm_valtype_vec_new_empty(&params);
  wasm_valtype_vec_new(&results, 2, rs);
  return wasm_functype_new(&params, &results);
}

static inline own wasm_functype_t* wasm_functype_new_1_2(
  own wasm_valtype_t* p, own wasm_valtype_t* r1, own wasm_valtype_t* r2
) {
  wasm_valtype_t* ps[1] = {p};
  wasm_valtype_t* rs[2] = {r1, r2};
  wasm_valtype_vec_t params, results;
  wasm_valtype_vec_new(&params, 1, ps);
  wasm_valtype_vec_new(&results, 2, rs);
  return wasm_functype_new(&params, &results);
}

static inline own wasm_functype_t* wasm_functype_new_2_2(
  own wasm_valtype_t* p1, own wasm_valtype_t* p2,
  own wasm_valtype_t* r1, own wasm_valtype_t* r2
) {
  wasm_valtype_t* ps[2] = {p1, p2};
  wasm_valtype_t* rs[2] = {r1, r2};
  wasm_valtype_vec_t params, results;
  wasm_valtype_vec_new(&params, 2, ps);
  wasm_valtype_vec_new(&results, 2, rs);
  return wasm_functype_new(&params, &results);
}

static inline own wasm_functype_t* wasm_functype_new_3_2(
  own wasm_valtype_t* p1, own wasm_valtype_t* p2, own wasm_valtype_t* p3,
  own wasm_valtype_t* r1, own wasm_valtype_t* r2
) {
  wasm_valtype_t* ps[3] = {p1, p2, p3};
  wasm_valtype_t* rs[2] = {r1, r2};
  wasm_valtype_vec_t params, results;
  wasm_valtype_vec_new(&params, 3, ps);
  wasm_valtype_vec_new(&results, 2, rs);
  return wasm_functype_new(&params, &results);
}


// Value construction short-hands

static inline void wasm_val_init_ptr(own wasm_val_t* out, void* p) {
#if UINTPTR_MAX == UINT32_MAX
  out->kind = WASM_I32;
  out->of.i32 = (intptr_t)p;
#elif UINTPTR_MAX == UINT64_MAX
  out->kind = WASM_I64;
  out->of.i64 = (intptr_t)p;
#endif
}

static inline void* wasm_val_ptr(const wasm_val_t* val) {
#if UINTPTR_MAX == UINT32_MAX
  return (void*)(intptr_t)val->of.i32;
#elif UINTPTR_MAX == UINT64_MAX
  return (void*)(intptr_t)val->of.i64;
#endif
}

#define WASM_I32_VAL(i) {.kind = WASM_I32, .of = {.i32 = i}}
#define WASM_I64_VAL(i) {.kind = WASM_I64, .of = {.i64 = i}}
#define WASM_F32_VAL(z) {.kind = WASM_F32, .of = {.f32 = z}}
#define WASM_F64_VAL(z) {.kind = WASM_F64, .of = {.f64 = z}}
#define WASM_REF_VAL(r) {.kind = WASM_EXTERNREF, .of = {.ref = r}}
#define WASM_INIT_VAL {.kind = WASM_EXTERNREF, .of = {.ref = NULL}}


///////////////////////////////////////////////////////////////////////////////

#undef own

#ifdef __cplusplus
}  // extern "C"
#endif

#endif  // #ifdef WASM_H
