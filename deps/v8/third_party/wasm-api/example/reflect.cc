#include <iostream>
#include <fstream>
#include <cstdlib>
#include <string>
#include <cinttypes>

#include "wasm.hh"


auto operator<<(std::ostream& out, wasm::Mutability mut) -> std::ostream& {
  switch (mut) {
    case wasm::Mutability::VAR:
      return out << "var";
    case wasm::Mutability::CONST:
      return out << "const";
  }
  return out;
}

auto operator<<(std::ostream& out, wasm::Limits limits) -> std::ostream& {
  out << limits.min;
  if (limits.max < wasm::Limits(0).max) out << " " << limits.max;
  return out;
}

auto operator<<(std::ostream& out, const wasm::ValType& type) -> std::ostream& {
  switch (type.kind()) {
    case wasm::ValKind::I32:
      return out << "i32";
    case wasm::ValKind::I64:
      return out << "i64";
    case wasm::ValKind::F32:
      return out << "f32";
    case wasm::ValKind::F64:
      return out << "f64";
    case wasm::ValKind::EXTERNREF:
      return out << "externref";
    case wasm::ValKind::FUNCREF:
      return out << "funcref";
  }
  return out;
}

auto operator<<(std::ostream& out, const wasm::ownvec<wasm::ValType>& types) -> std::ostream& {
  bool first = true;
  for (size_t i = 0; i < types.size(); ++i) {
    if (first) {
      first = false;
    } else {
      out << " ";
    }
    out << *types[i].get();
  }
  return out;
}

auto operator<<(std::ostream& out, const wasm::ExternType& type) -> std::ostream& {
  switch (type.kind()) {
    case wasm::ExternKind::FUNC: {
      out << "func " << type.func()->params() << " -> " << type.func()->results();
    } break;
    case wasm::ExternKind::GLOBAL: {
      out << "global " << type.global()->mutability() << " " << *type.global()->content();
    } break;
    case wasm::ExternKind::TABLE: {
      out << "table " << type.table()->limits() << " " << *type.table()->element();
    } break;
    case wasm::ExternKind::MEMORY: {
      out << "memory " << type.memory()->limits();
    } break;
  }
  return out;
}

auto operator<<(std::ostream& out, const wasm::Name& name) -> std::ostream& {
  out << "\"" << std::string(name.get(), name.size()) << "\"";
  return out;
}


void run() {
  // Initialize.
  std::cout << "Initializing..." << std::endl;
  auto engine = wasm::Engine::make();
  auto store_ = wasm::Store::make(engine.get());
  auto store = store_.get();

  // Load binary.
  std::cout << "Loading binary..." << std::endl;
  std::ifstream file("reflect.wasm");
  file.seekg(0, std::ios_base::end);
  auto file_size = file.tellg();
  file.seekg(0);
  auto binary = wasm::vec<byte_t>::make_uninitialized(file_size);
  file.read(binary.get(), file_size);
  file.close();
  if (file.fail()) {
    std::cout << "> Error loading module!" << std::endl;
    exit(1);
  }

  // Compile.
  std::cout << "Compiling module..." << std::endl;
  auto module = wasm::Module::make(store, binary);
  if (!module) {
    std::cout << "> Error compiling module!" << std::endl;
    exit(1);
  }

  // Instantiate.
  std::cout << "Instantiating module..." << std::endl;
  auto imports = wasm::vec<wasm::Extern*>::make();
  auto instance = wasm::Instance::make(store, module.get(), imports);
  if (!instance) {
    std::cout << "> Error instantiating module!" << std::endl;
    exit(1);
  }

  // Extract exports.
  std::cout << "Extracting export..." << std::endl;
  auto export_types = module->exports();
  auto exports = instance->exports();
  assert(exports.size() == export_types.size());

  for (size_t i = 0; i < exports.size(); ++i) {
    assert(exports[i]->kind() == export_types[i]->type()->kind());
    std::cout << "> export " << i << " " << export_types[i]->name() << std::endl;
    std::cout << ">> initial: " << *export_types[i]->type() << std::endl;
    std::cout << ">> current: " << *exports[i]->type() << std::endl;
    if (exports[i]->kind() == wasm::ExternKind::FUNC) {
      auto func = exports[i]->func();
      std::cout << ">> in-arity: " << func->param_arity();
      std::cout << ", out-arity: " << func->result_arity() << std::endl;
    }
  }

  // Shut down.
  std::cout << "Shutting down..." << std::endl;
}


int main(int argc, const char* argv[]) {
  run();
  std::cout << "Done." << std::endl;
  return 0;
}

