#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>

#include "wasm.h"

#define own


void print_frame(wasm_frame_t* frame) {
  printf("> %p @ 0x%zx = %"PRIu32".0x%zx\n",
    wasm_frame_instance(frame),
    wasm_frame_module_offset(frame),
    wasm_frame_func_index(frame),
    wasm_frame_func_offset(frame)
  );
}


int main(int argc, const char* argv[]) {
  // Initialize.
  printf("Initializing...\n");
  wasm_engine_t* engine = wasm_engine_new();
  wasm_store_t* store = wasm_store_new(engine);

  // Load binary.
  printf("Loading binary...\n");
  FILE* file = fopen("start.wasm", "rb");
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
  own wasm_trap_t* trap = NULL;
  own wasm_instance_t* instance =
    wasm_instance_new(store, module, &imports, &trap);
  if (instance || !trap) {
    printf("> Error instantiating module, expected trap!\n");
    return 1;
  }

  wasm_module_delete(module);

  // Print result.
  printf("Printing message...\n");
  own wasm_name_t message;
  wasm_trap_message(trap, &message);
  printf("> %s\n", message.data);

  printf("Printing origin...\n");
  own wasm_frame_t* frame = wasm_trap_origin(trap);
  if (frame) {
    print_frame(frame);
    wasm_frame_delete(frame);
  } else {
    printf("> Empty origin.\n");
  }

  printf("Printing trace...\n");
  own wasm_frame_vec_t trace;
  wasm_trap_trace(trap, &trace);
  if (trace.size > 0) {
    for (size_t i = 0; i < trace.size; ++i) {
      print_frame(trace.data[i]);
    }
  } else {
    printf("> Empty trace.\n");
  }

  wasm_frame_vec_delete(&trace);
  wasm_trap_delete(trap);
  wasm_name_delete(&message);

  // Shut down.
  printf("Shutting down...\n");
  wasm_store_delete(store);
  wasm_engine_delete(engine);

  // All done.
  printf("Done.\n");
  return 0;
}
