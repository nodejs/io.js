#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>

#include "wasm.h"

#define own


wasm_memory_t* get_export_memory(const wasm_extern_vec_t* exports, size_t i) {
  if (exports->size <= i || !wasm_extern_as_memory(exports->data[i])) {
    printf("> Error accessing memory export %zu!\n", i);
    exit(1);
  }
  return wasm_extern_as_memory(exports->data[i]);
}

wasm_func_t* get_export_func(const wasm_extern_vec_t* exports, size_t i) {
  if (exports->size <= i || !wasm_extern_as_func(exports->data[i])) {
    printf("> Error accessing function export %zu!\n", i);
    exit(1);
  }
  return wasm_extern_as_func(exports->data[i]);
}


void check(bool success) {
  if (!success) {
    printf("> Error, expected success\n");
    exit(1);
  }
}

void check_call(wasm_func_t* func, int i, wasm_val_t args[], int32_t expected) {
  wasm_val_t r = WASM_INIT_VAL;
  wasm_val_vec_t args_ = {i, args};
  wasm_val_vec_t results = {1, &r};
  if (wasm_func_call(func, &args_, &results) || r.of.i32 != expected) {
    printf("> Error on result\n");
    exit(1);
  }
}

void check_call0(wasm_func_t* func, int32_t expected) {
  check_call(func, 0, NULL, expected);
}

void check_call1(wasm_func_t* func, int32_t arg, int32_t expected) {
  wasm_val_t args[] = { WASM_I32_VAL(arg) };
  check_call(func, 1, args, expected);
}

void check_call2(wasm_func_t* func, int32_t arg1, int32_t arg2, int32_t expected) {
  wasm_val_t args[] = { WASM_I32_VAL(arg1), WASM_I32_VAL(arg2) };
  check_call(func, 2, args, expected);
}

void check_ok(wasm_func_t* func, int i, wasm_val_t args[]) {
  wasm_val_vec_t args_ = {i, args};
  wasm_val_vec_t results = {0, NULL};
  if (wasm_func_call(func, &args_, &results)) {
    printf("> Error on result, expected empty\n");
    exit(1);
  }
}

void check_ok2(wasm_func_t* func, int32_t arg1, int32_t arg2) {
  wasm_val_t args[] = { WASM_I32_VAL(arg1), WASM_I32_VAL(arg2) };
  check_ok(func, 2, args);
}

void check_trap(wasm_func_t* func, int i, wasm_val_t args[]) {
  wasm_val_t r = WASM_INIT_VAL;
  wasm_val_vec_t args_ = {i, args};
  wasm_val_vec_t results = {1, &r};
  own wasm_trap_t* trap = wasm_func_call(func, &args_, &results);
  if (! trap) {
    printf("> Error on result, expected trap\n");
    exit(1);
  }
  wasm_trap_delete(trap);
}

void check_trap1(wasm_func_t* func, int32_t arg) {
  wasm_val_t args[] = { WASM_I32_VAL(arg) };
  check_trap(func, 1, args);
}

void check_trap2(wasm_func_t* func, int32_t arg1, int32_t arg2) {
  wasm_val_t args[] = { WASM_I32_VAL(arg1), WASM_I32_VAL(arg2) };
  check_trap(func, 2, args);
}


int main(int argc, const char* argv[]) {
  // Initialize.
  printf("Initializing...\n");
  wasm_engine_t* engine = wasm_engine_new();
  wasm_store_t* store = wasm_store_new(engine);

  // Load binary.
  printf("Loading binary...\n");
  FILE* file = fopen("memory.wasm", "rb");
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

  // Instantiate.
  printf("Instantiating module...\n");
  wasm_extern_vec_t imports = WASM_EMPTY_VEC;
  own wasm_instance_t* instance =
    wasm_instance_new(store, module, &imports, NULL);
  if (!instance) {
    printf("> Error instantiating module!\n");
    return 1;
  }

  // Extract export.
  printf("Extracting exports...\n");
  own wasm_extern_vec_t exports;
  wasm_instance_exports(instance, &exports);
  size_t i = 0;
  wasm_memory_t* memory = get_export_memory(&exports, i++);
  wasm_func_t* size_func = get_export_func(&exports, i++);
  wasm_func_t* load_func = get_export_func(&exports, i++);
  wasm_func_t* store_func = get_export_func(&exports, i++);

  wasm_module_delete(module);

  // Try cloning.
  own wasm_memory_t* copy = wasm_memory_copy(memory);
  assert(wasm_memory_same(memory, copy));
  wasm_memory_delete(copy);

  // Check initial memory.
  printf("Checking memory...\n");
  check(wasm_memory_size(memory) == 2);
  check(wasm_memory_data_size(memory) == 0x20000);
  check(wasm_memory_data(memory)[0] == 0);
  check(wasm_memory_data(memory)[0x1000] == 1);
  check(wasm_memory_data(memory)[0x1003] == 4);

  check_call0(size_func, 2);
  check_call1(load_func, 0, 0);
  check_call1(load_func, 0x1000, 1);
  check_call1(load_func, 0x1003, 4);
  check_call1(load_func, 0x1ffff, 0);
  check_trap1(load_func, 0x20000);

  // Mutate memory.
  printf("Mutating memory...\n");
  wasm_memory_data(memory)[0x1003] = 5;
  check_ok2(store_func, 0x1002, 6);
  check_trap2(store_func, 0x20000, 0);

  check(wasm_memory_data(memory)[0x1002] == 6);
  check(wasm_memory_data(memory)[0x1003] == 5);
  check_call1(load_func, 0x1002, 6);
  check_call1(load_func, 0x1003, 5);

  // Grow memory.
  printf("Growing memory...\n");
  check(wasm_memory_grow(memory, 1));
  check(wasm_memory_size(memory) == 3);
  check(wasm_memory_data_size(memory) == 0x30000);

  check_call1(load_func, 0x20000, 0);
  check_ok2(store_func, 0x20000, 0);
  check_trap1(load_func, 0x30000);
  check_trap2(store_func, 0x30000, 0);

  check(! wasm_memory_grow(memory, 1));
  check(wasm_memory_grow(memory, 0));

  wasm_extern_vec_delete(&exports);
  wasm_instance_delete(instance);

  // Create stand-alone memory.
  // TODO(wasm+): Once Wasm allows multiple memories, turn this into import.
  printf("Creating stand-alone memory...\n");
  wasm_limits_t limits = {5, 5};
  own wasm_memorytype_t* memorytype = wasm_memorytype_new(&limits);
  own wasm_memory_t* memory2 = wasm_memory_new(store, memorytype);
  check(wasm_memory_size(memory2) == 5);
  check(! wasm_memory_grow(memory2, 1));
  check(wasm_memory_grow(memory2, 0));

  wasm_memorytype_delete(memorytype);
  wasm_memory_delete(memory2);

  // Shut down.
  printf("Shutting down...\n");
  wasm_store_delete(store);
  wasm_engine_delete(engine);

  // All done.
  printf("Done.\n");
  return 0;
}
