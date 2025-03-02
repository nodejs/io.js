#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>

#include "wasm.h"

#define own

void print_mutability(wasm_mutability_t mut) {
  switch (mut) {
    case WASM_VAR: printf("var"); break;
    case WASM_CONST: printf("const"); break;
  }
}

void print_limits(const wasm_limits_t* limits) {
  printf("%ud", limits->min);
  if (limits->max < wasm_limits_max_default) printf(" %ud", limits->max);
}

void print_valtype(const wasm_valtype_t* type) {
  switch (wasm_valtype_kind(type)) {
    case WASM_I32: printf("i32"); break;
    case WASM_I64: printf("i64"); break;
    case WASM_F32: printf("f32"); break;
    case WASM_F64: printf("f64"); break;
    case WASM_EXTERNREF: printf("externref"); break;
    case WASM_FUNCREF: printf("funcref"); break;
  }
}

void print_valtypes(const wasm_valtype_vec_t* types) {
  bool first = true;
  for (size_t i = 0; i < types->size; ++i) {
    if (first) {
      first = false;
    } else {
      printf(" ");
    }
    print_valtype(types->data[i]);
  }
}

void print_externtype(const wasm_externtype_t* type) {
  switch (wasm_externtype_kind(type)) {
    case WASM_EXTERN_FUNC: {
      const wasm_functype_t* functype =
        wasm_externtype_as_functype_const(type);
      printf("func ");
      print_valtypes(wasm_functype_params(functype));
      printf(" -> ");
      print_valtypes(wasm_functype_results(functype));
    } break;
    case WASM_EXTERN_GLOBAL: {
      const wasm_globaltype_t* globaltype =
        wasm_externtype_as_globaltype_const(type);
      printf("global ");
      print_mutability(wasm_globaltype_mutability(globaltype));
      printf(" ");
      print_valtype(wasm_globaltype_content(globaltype));
    } break;
    case WASM_EXTERN_TABLE: {
      const wasm_tabletype_t* tabletype =
        wasm_externtype_as_tabletype_const(type);
      printf("table ");
      print_limits(wasm_tabletype_limits(tabletype));
      printf(" ");
      print_valtype(wasm_tabletype_element(tabletype));
    } break;
    case WASM_EXTERN_MEMORY: {
      const wasm_memorytype_t* memorytype =
        wasm_externtype_as_memorytype_const(type);
      printf("memory ");
      print_limits(wasm_memorytype_limits(memorytype));
    } break;
  }
}

void print_name(const wasm_name_t* name) {
  printf("\"%.*s\"", (int)name->size, name->data);
}


int main(int argc, const char* argv[]) {
  // Initialize.
  printf("Initializing...\n");
  wasm_engine_t* engine = wasm_engine_new();
  wasm_store_t* store = wasm_store_new(engine);

  // Load binary.
  printf("Loading binary...\n");
  FILE* file = fopen("reflect.wasm", "rb");
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
  printf("Extracting export...\n");
  own wasm_exporttype_vec_t export_types;
  own wasm_extern_vec_t exports;
  wasm_module_exports(module, &export_types);
  wasm_instance_exports(instance, &exports);
  assert(exports.size == export_types.size);

  for (size_t i = 0; i < exports.size; ++i) {
    assert(wasm_extern_kind(exports.data[i]) ==
      wasm_externtype_kind(wasm_exporttype_type(export_types.data[i])));
    printf("> export %zu ", i);
    print_name(wasm_exporttype_name(export_types.data[i]));
    printf("\n");
    printf(">> initial: ");
    print_externtype(wasm_exporttype_type(export_types.data[i]));
    printf("\n");
    printf(">> current: ");
    own wasm_externtype_t* current = wasm_extern_type(exports.data[i]);
    print_externtype(current);
    wasm_externtype_delete(current);
    printf("\n");
    if (wasm_extern_kind(exports.data[i]) == WASM_EXTERN_FUNC) {
      wasm_func_t* func = wasm_extern_as_func(exports.data[i]);
      printf(">> in-arity: %zu", wasm_func_param_arity(func));
      printf(", out-arity: %zu\n", wasm_func_result_arity(func));
    }
  }

  wasm_module_delete(module);
  wasm_instance_delete(instance);
  wasm_extern_vec_delete(&exports);
  wasm_exporttype_vec_delete(&export_types);

  // Shut down.
  printf("Shutting down...\n");
  wasm_store_delete(store);
  wasm_engine_delete(engine);

  // All done.
  printf("Done.\n");
  return 0;
}
