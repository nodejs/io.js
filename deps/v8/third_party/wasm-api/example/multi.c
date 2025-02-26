#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>

#include "wasm.h"

#define own

// A function to be called from Wasm code.
own wasm_trap_t* callback(
  const wasm_val_vec_t* args, wasm_val_vec_t* results
) {
  printf("Calling back...\n> ");
  printf("> %"PRIu32" %"PRIu64" %"PRIu64" %"PRIu32"\n",
    args->data[0].of.i32, args->data[1].of.i64,
    args->data[2].of.i64, args->data[3].of.i32);
  printf("\n");

  wasm_val_copy(&results->data[0], &args->data[3]);
  wasm_val_copy(&results->data[1], &args->data[1]);
  wasm_val_copy(&results->data[2], &args->data[2]);
  wasm_val_copy(&results->data[3], &args->data[0]);
  return NULL;
}


// A function closure.
own wasm_trap_t* closure_callback(
  void* env, const wasm_val_vec_t* args, wasm_val_vec_t* results
) {
  int i = *(int*)env;
  printf("Calling back closure...\n");
  printf("> %d\n", i);

  results->data[0].kind = WASM_I32;
  results->data[0].of.i32 = (int32_t)i;
  return NULL;
}


int main(int argc, const char* argv[]) {
  // Initialize.
  printf("Initializing...\n");
  wasm_engine_t* engine = wasm_engine_new();
  wasm_store_t* store = wasm_store_new(engine);

  // Load binary.
  printf("Loading binary...\n");
  FILE* file = fopen("multi.wasm", "rb");
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

  // Create external print functions.
  printf("Creating callback...\n");
  wasm_valtype_t* types[4] = {
    wasm_valtype_new_i32(), wasm_valtype_new_i64(),
    wasm_valtype_new_i64(), wasm_valtype_new_i32()
  };
  own wasm_valtype_vec_t tuple1, tuple2;
  wasm_valtype_vec_new(&tuple1, 4, types);
  wasm_valtype_vec_copy(&tuple2, &tuple1);
  own wasm_functype_t* callback_type = wasm_functype_new(&tuple1, &tuple2);
  own wasm_func_t* callback_func =
    wasm_func_new(store, callback_type, callback);

  wasm_functype_delete(callback_type);

  // Instantiate.
  printf("Instantiating module...\n");
  wasm_extern_t* externs[] = { wasm_func_as_extern(callback_func) };
  wasm_extern_vec_t imports = WASM_ARRAY_VEC(externs);
  own wasm_instance_t* instance =
    wasm_instance_new(store, module, &imports, NULL);
  if (!instance) {
    printf("> Error instantiating module!\n");
    return 1;
  }

  wasm_func_delete(callback_func);

  // Extract export.
  printf("Extracting export...\n");
  own wasm_extern_vec_t exports;
  wasm_instance_exports(instance, &exports);
  if (exports.size == 0) {
    printf("> Error accessing exports!\n");
    return 1;
  }
  const wasm_func_t* run_func = wasm_extern_as_func(exports.data[0]);
  if (run_func == NULL) {
    printf("> Error accessing export!\n");
    return 1;
  }

  wasm_module_delete(module);
  wasm_instance_delete(instance);

  // Call.
  printf("Calling export...\n");
  wasm_val_t vals[4] = {
    WASM_I32_VAL(1), WASM_I64_VAL(2), WASM_I64_VAL(3), WASM_I32_VAL(4)
  };
  wasm_val_t res[4] = {
    WASM_INIT_VAL, WASM_INIT_VAL, WASM_INIT_VAL, WASM_INIT_VAL
  };
  wasm_val_vec_t args = WASM_ARRAY_VEC(vals);
  wasm_val_vec_t results = WASM_ARRAY_VEC(res);
  if (wasm_func_call(run_func, &args, &results)) {
    printf("> Error calling function!\n");
    return 1;
  }

  wasm_extern_vec_delete(&exports);

  // Print result.
  printf("Printing result...\n");
  printf("> %"PRIu32" %"PRIu64" %"PRIu64" %"PRIu32"\n",
    res[0].of.i32, res[1].of.i64, res[2].of.i64, res[3].of.i32);

  assert(res[0].of.i32 == 4);
  assert(res[1].of.i64 == 3);
  assert(res[2].of.i64 == 2);
  assert(res[3].of.i32 == 1);

  // Shut down.
  printf("Shutting down...\n");
  wasm_store_delete(store);
  wasm_engine_delete(engine);

  // All done.
  printf("Done.\n");
  return 0;
}
