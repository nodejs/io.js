#include <iostream>
#include <fstream>
#include <cstdlib>
#include <string>
#include <cinttypes>

#include "wasm.hh"

// A function to be called from Wasm code.
auto fail_callback(void* env, const wasm::vec<wasm::Val>& args,
                   wasm::vec<wasm::Val>& results) -> wasm::own<wasm::Trap> {
  std::cout << "Calling back..." << std::endl;
  auto store = reinterpret_cast<wasm::Store*>(env);
  auto message = wasm::Name::make_nt(std::string("callback abort"));
  return wasm::Trap::make(store, message);
}

void print_frame(const wasm::Frame* frame) {
  std::cout << "> " << frame->instance();
  std::cout << " @ 0x" << std::hex << frame->module_offset();
  std::cout << " = " << frame->func_index();
  std::cout << ".0x" << std::hex << frame->func_offset() << std::endl;
}


void run() {
  // Initialize.
  std::cout << "Initializing..." << std::endl;
  auto engine = wasm::Engine::make();
  auto store_ = wasm::Store::make(engine.get());
  auto store = store_.get();

  // Load binary.
  std::cout << "Loading binary..." << std::endl;
  std::ifstream file("trap.wasm");
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

  // Create external print functions.
  std::cout << "Creating callback..." << std::endl;
  auto fail_type =
      wasm::FuncType::make(wasm::ownvec<wasm::ValType>::make(),
                           wasm::ownvec<wasm::ValType>::make(
                               wasm::ValType::make(wasm::ValKind::I32)));
  auto fail_func =
    wasm::Func::make(store, fail_type.get(), fail_callback, store);

  // Instantiate.
  std::cout << "Instantiating module..." << std::endl;
  auto imports = wasm::vec<wasm::Extern*>::make(fail_func.get());
  auto instance = wasm::Instance::make(store, module.get(), imports);
  if (!instance) {
    std::cout << "> Error instantiating module!" << std::endl;
    exit(1);
  }

  // Extract export.
  std::cout << "Extracting exports..." << std::endl;
  auto exports = instance->exports();
  if (exports.size() < 2 || exports[0]->kind() != wasm::ExternKind::FUNC ||
      !exports[0]->func() || exports[1]->kind() != wasm::ExternKind::FUNC ||
      !exports[1]->func()) {
    std::cout << "> Error accessing exports!" << std::endl;
    exit(1);
  }

  // Call.
  for (size_t i = 0; i < 2; ++i) {
    std::cout << "Calling export " << i << "..." << std::endl;
    auto args = wasm::vec<wasm::Val>::make();
    auto results = wasm::vec<wasm::Val>::make();
    auto trap = exports[i]->func()->call(args, results);
    if (!trap) {
      std::cout << "> Error calling function, expected trap!" << std::endl;
      exit(1);
    }

    std::cout << "Printing message..." << std::endl;
    std::cout << "> " << trap->message().get() << std::endl;

    std::cout << "Printing origin..." << std::endl;
    auto frame = trap->origin();
    if (frame) {
      print_frame(frame.get());
    } else {
      std::cout << "> Empty origin." << std::endl;
    }

    std::cout << "Printing trace..." << std::endl;
    auto trace = trap->trace();
    if (trace.size() > 0) {
      for (size_t i = 0; i < trace.size(); ++i) {
        print_frame(trace[i].get());
      }
    } else {
      std::cout << "> Empty trace." << std::endl;
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

