#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>

#include "wasm.h"

#define own

wasm_global_t* get_export_global(const wasm_extern_vec_t* exports, size_t i) {
  if (exports->size <= i || !wasm_extern_as_global(exports->data[i])) {
    printf("> Error accessing global export %zu!\n", i);
    exit(1);
  }
  return wasm_extern_as_global(exports->data[i]);
}

wasm_func_t* get_export_func(const wasm_extern_vec_t* exports, size_t i) {
  if (exports->size <= i || !wasm_extern_as_func(exports->data[i])) {
    printf("> Error accessing function export %zu!\n", i);
    exit(1);
  }
  return wasm_extern_as_func(exports->data[i]);
}


#define check(val, type, expected) \
  if (val.of.type != expected) { \
    printf("> Error reading value\n"); \
    exit(1); \
  }

#define check_global(global, type, expected) \
  { \
    wasm_val_t val; \
    wasm_global_get(global, &val); \
    check(val, type, expected); \
  }

#define check_call(func, type, expected) \
  { \
    wasm_val_t vs[1]; \
    wasm_val_vec_t args = WASM_EMPTY_VEC; \
    wasm_val_vec_t results = WASM_ARRAY_VEC(vs); \
    wasm_func_call(func, &args, &results); \
    check(vs[0], type, expected); \
  }


int main(int argc, const char* argv[]) {
  // Initialize.
  printf("Initializing...\n");
  wasm_engine_t* engine = wasm_engine_new();
  wasm_store_t* store = wasm_store_new(engine);

  // Load binary.
  printf("Loading binary...\n");
  FILE* file = fopen("global.wasm", "rb");
  if (!file) {
    printf("> Error loading module!\n");
    return 1;
  }
  fseek(file, 0L, SEEK_END);
  size_t file_size = ftell(file);
  fseek(file, 0L, SEEK_SET);
  wasm_byte_vec_t binary;
  wasm_byte_vec_new_uninitialized(&binary, file_size);
  if (fread(binary.data, file_size, 1, file) != 1) {
    printf("> Error loading module!\n");
    return 1;
  }
  fclose(file);

  // Compile.
  printf("Compiling module...\n");
  own wasm_module_t* module = wasm_module_new(store, &binary);
  if (!module) {
    printf("> Error compiling module!\n");
    return 1;
  }

  wasm_byte_vec_delete(&binary);

  // Create external globals.
  printf("Creating globals...\n");
  own wasm_globaltype_t* const_f32_type = wasm_globaltype_new(
    wasm_valtype_new(WASM_F32), WASM_CONST);
  own wasm_globaltype_t* const_i64_type = wasm_globaltype_new(
    wasm_valtype_new(WASM_I64), WASM_CONST);
  own wasm_globaltype_t* var_f32_type = wasm_globaltype_new(
    wasm_valtype_new(WASM_F32), WASM_VAR);
  own wasm_globaltype_t* var_i64_type = wasm_globaltype_new(
    wasm_valtype_new(WASM_I64), WASM_VAR);

  wasm_val_t val_f32_1 = WASM_F32_VAL(1);
  own wasm_global_t* const_f32_import =
    wasm_global_new(store, const_f32_type, &val_f32_1);
  wasm_val_t val_i64_2 = WASM_I64_VAL(2);
  own wasm_global_t* const_i64_import =
    wasm_global_new(store, const_i64_type, &val_i64_2);
  wasm_val_t val_f32_3 = WASM_F32_VAL(3);
  own wasm_global_t* var_f32_import =
    wasm_global_new(store, var_f32_type, &val_f32_3);
  wasm_val_t val_i64_4 = WASM_I64_VAL(4);
  own wasm_global_t* var_i64_import =
    wasm_global_new(store, var_i64_type, &val_i64_4);

  wasm_globaltype_delete(const_f32_type);
  wasm_globaltype_delete(const_i64_type);
  wasm_globaltype_delete(var_f32_type);
  wasm_globaltype_delete(var_i64_type);

  // Instantiate.
  printf("Instantiating module...\n");
  wasm_extern_t* externs[] = {
    wasm_global_as_extern(const_f32_import),
    wasm_global_as_extern(const_i64_import),
    wasm_global_as_extern(var_f32_import),
    wasm_global_as_extern(var_i64_import)
  };
  wasm_extern_vec_t imports = WASM_ARRAY_VEC(externs);
  own wasm_instance_t* instance =
    wasm_instance_new(store, module, &imports, NULL);
  if (!instance) {
    printf("> Error instantiating module!\n");
    return 1;
  }

  wasm_module_delete(module);

  // Extract export.
  printf("Extracting exports...\n");
  own wasm_extern_vec_t exports;
  wasm_instance_exports(instance, &exports);
  size_t i = 0;
  wasm_global_t* const_f32_export = get_export_global(&exports, i++);
  wasm_global_t* const_i64_export = get_export_global(&exports, i++);
  wasm_global_t* var_f32_export = get_export_global(&exports, i++);
  wasm_global_t* var_i64_export = get_export_global(&exports, i++);
  wasm_func_t* get_const_f32_import = get_export_func(&exports, i++);
  wasm_func_t* get_const_i64_import = get_export_func(&exports, i++);
  wasm_func_t* get_var_f32_import = get_export_func(&exports, i++);
  wasm_func_t* get_var_i64_import = get_export_func(&exports, i++);
  wasm_func_t* get_const_f32_export = get_export_func(&exports, i++);
  wasm_func_t* get_const_i64_export = get_export_func(&exports, i++);
  wasm_func_t* get_var_f32_export = get_export_func(&exports, i++);
  wasm_func_t* get_var_i64_export = get_export_func(&exports, i++);
  wasm_func_t* set_var_f32_import = get_export_func(&exports, i++);
  wasm_func_t* set_var_i64_import = get_export_func(&exports, i++);
  wasm_func_t* set_var_f32_export = get_export_func(&exports, i++);
  wasm_func_t* set_var_i64_export = get_export_func(&exports, i++);

  // Try cloning.
  own wasm_global_t* copy = wasm_global_copy(var_f32_import);
  assert(wasm_global_same(var_f32_import, copy));
  wasm_global_delete(copy);

  // Interact.
  printf("Accessing globals...\n");

  // Check initial values.
  check_global(const_f32_import, f32, 1);
  check_global(const_i64_import, i64, 2);
  check_global(var_f32_import, f32, 3);
  check_global(var_i64_import, i64, 4);
  check_global(const_f32_export, f32, 5);
  check_global(const_i64_export, i64, 6);
  check_global(var_f32_export, f32, 7);
  check_global(var_i64_export, i64, 8);

  check_call(get_const_f32_import, f32, 1);
  check_call(get_const_i64_import, i64, 2);
  check_call(get_var_f32_import, f32, 3);
  check_call(get_var_i64_import, i64, 4);
  check_call(get_const_f32_export, f32, 5);
  check_call(get_const_i64_export, i64, 6);
  check_call(get_var_f32_export, f32, 7);
  check_call(get_var_i64_export, i64, 8);

  // Modify variables through API and check again.
  wasm_val_t val33 = WASM_F32_VAL(33);
  wasm_global_set(var_f32_import, &val33);
  wasm_val_t val34 = WASM_I64_VAL(34);
  wasm_global_set(var_i64_import, &val34);
  wasm_val_t val37 = WASM_F32_VAL(37);
  wasm_global_set(var_f32_export, &val37);
  wasm_val_t val38 = WASM_I64_VAL(38);
  wasm_global_set(var_i64_export, &val38);

  check_global(var_f32_import, f32, 33);
  check_global(var_i64_import, i64, 34);
  check_global(var_f32_export, f32, 37);
  check_global(var_i64_export, i64, 38);

  check_call(get_var_f32_import, f32, 33);
  check_call(get_var_i64_import, i64, 34);
  check_call(get_var_f32_export, f32, 37);
  check_call(get_var_i64_export, i64, 38);

  // Modify variables through calls and check again.
  wasm_val_vec_t res = WASM_EMPTY_VEC;
  wasm_val_t vs73[] = { WASM_F32_VAL(73) };
  wasm_val_vec_t args73 = WASM_ARRAY_VEC(vs73);
  wasm_func_call(set_var_f32_import, &args73, &res);
  wasm_val_t vs74[] = { WASM_I64_VAL(74) };
  wasm_val_vec_t args74 = WASM_ARRAY_VEC(vs74);
  wasm_func_call(set_var_i64_import, &args74, &res);
  wasm_val_t vs77[] = { WASM_F32_VAL(77) };
  wasm_val_vec_t args77 = WASM_ARRAY_VEC(vs77);
  wasm_func_call(set_var_f32_export, &args77, &res);
  wasm_val_t vs78[] = { WASM_I64_VAL(78) };
  wasm_val_vec_t args78 = WASM_ARRAY_VEC(vs78);
  wasm_func_call(set_var_i64_export, &args78, &res);

  check_global(var_f32_import, f32, 73);
  check_global(var_i64_import, i64, 74);
  check_global(var_f32_export, f32, 77);
  check_global(var_i64_export, i64, 78);

  check_call(get_var_f32_import, f32, 73);
  check_call(get_var_i64_import, i64, 74);
  check_call(get_var_f32_export, f32, 77);
  check_call(get_var_i64_export, i64, 78);

  wasm_global_delete(const_f32_import);
  wasm_global_delete(const_i64_import);
  wasm_global_delete(var_f32_import);
  wasm_global_delete(var_i64_import);
  wasm_extern_vec_delete(&exports);
  wasm_instance_delete(instance);

  // Shut down.
  printf("Shutting down...\n");
  wasm_store_delete(store);
  wasm_engine_delete(engine);

  // All done.
  printf("Done.\n");
  return 0;
}
