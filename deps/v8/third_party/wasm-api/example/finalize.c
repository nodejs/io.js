#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>

#include "wasm.h"

#define own

const int iterations = 100000;

int live_count = 0;

void finalize(void* data) {
  intptr_t i = (intptr_t)data;
  if (i % (iterations / 10) == 0) printf("Finalizing #%" PRIdPTR "...\n", i);
  --live_count;
}

void run_in_store(wasm_store_t* store) {
  // Load binary.
  printf("Loading binary...\n");
  FILE* file = fopen("finalize.wasm", "rb");
  if (!file) {
    printf("> Error loading module!\n");
    exit(1);
  }
  fseek(file, 0L, SEEK_END);
  size_t file_size = ftell(file);
  fseek(file, 0L, SEEK_SET);
  wasm_byte_vec_t binary;
  wasm_byte_vec_new_uninitialized(&binary, file_size);
  if (fread(binary.data, file_size, 1, file) != 1) {
    printf("> Error loading module!\n");
    exit(1);
  }
  fclose(file);

  // Compile.
  printf("Compiling module...\n");
  own wasm_module_t* module = wasm_module_new(store, &binary);
  if (!module) {
    printf("> Error compiling module!\n");
    exit(1);
  }

  wasm_byte_vec_delete(&binary);

  // Instantiate.
  printf("Instantiating modules...\n");
  for (int i = 0; i <= iterations; ++i) {
    if (i % (iterations / 10) == 0) printf("%d\n", i);
    wasm_extern_vec_t imports = WASM_EMPTY_VEC;
    own wasm_instance_t* instance =
      wasm_instance_new(store, module, &imports, NULL);
    if (!instance) {
      printf("> Error instantiating module %d!\n", i);
      exit(1);
    }
    void* data = (void*)(intptr_t)i;
    wasm_instance_set_host_info_with_finalizer(instance, data, &finalize);
    wasm_instance_delete(instance);
    ++live_count;
  }

  wasm_module_delete(module);
}

int main(int argc, const char* argv[]) {
  // Initialize.
  printf("Initializing...\n");
  wasm_engine_t* engine = wasm_engine_new();

  printf("Live count %d\n", live_count);
  printf("Creating store 1...\n");
  wasm_store_t* store1 = wasm_store_new(engine);

  printf("Running in store 1...\n");
  run_in_store(store1);
  printf("Live count %d\n", live_count);

  printf("Creating store 2...\n");
  wasm_store_t* store2 = wasm_store_new(engine);

  printf("Running in store 2...\n");
  run_in_store(store2);
  printf("Live count %d\n", live_count);

  printf("Deleting store 2...\n");
  wasm_store_delete(store2);
  printf("Live count %d\n", live_count);

  printf("Running in store 1...\n");
  run_in_store(store1);
  printf("Live count %d\n", live_count);

  printf("Deleting store 1...\n");
  wasm_store_delete(store1);
  printf("Live count %d\n", live_count);

  assert(live_count == 0);

  // Shut down.
  printf("Shutting down...\n");
  wasm_engine_delete(engine);

  // All done.
  printf("Done.\n");
  return 0;
}
