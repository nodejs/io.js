#include <iostream>
#include <fstream>
#include <thread>
#include <mutex>

#include "wasm.hh"

const int N_THREADS = 10;
const int N_REPS = 3;

// A function to be called from Wasm code.
auto callback(void* env, const wasm::vec<wasm::Val>& args,
              wasm::vec<wasm::Val>& results) -> wasm::own<wasm::Trap> {
  assert(args[0].kind() == wasm::ValKind::I32);
  std::lock_guard<std::mutex> lock(*reinterpret_cast<std::mutex*>(env));
  std::cout << "Thread " << args[0].i32() << " running..." << std::endl;
  std::cout.flush();
  return nullptr;
}

void run(
  wasm::Engine* engine, const wasm::Shared<wasm::Module>* shared,
  std::mutex* mutex, int id
) {
  // Create store.
  auto store_ = wasm::Store::make(engine);
  auto store = store_.get();

  // Obtain.
  auto module = wasm::Module::obtain(store, shared);
  if (!module) {
    std::lock_guard<std::mutex> lock(*mutex);
    std::cout << "> Error compiling module!" << std::endl;
    exit(1);
  }

  // Run the example N times.
  for (int i = 0; i < N_REPS; ++i) {
    std::this_thread::sleep_for(std::chrono::nanoseconds(100000));

    // Create imports.
    auto func_type =
        wasm::FuncType::make(wasm::ownvec<wasm::ValType>::make(
                                 wasm::ValType::make(wasm::ValKind::I32)),
                             wasm::ownvec<wasm::ValType>::make());
    auto func = wasm::Func::make(store, func_type.get(), callback, mutex);

    auto global_type = wasm::GlobalType::make(
        wasm::ValType::make(wasm::ValKind::I32), wasm::Mutability::CONST);
    auto global = wasm::Global::make(
      store, global_type.get(), wasm::Val::i32(i));

    // Instantiate.
    auto imports = wasm::vec<wasm::Extern*>::make(func.get(), global.get());
    auto instance = wasm::Instance::make(store, module.get(), imports);
    if (!instance) {
      std::lock_guard<std::mutex> lock(*mutex);
      std::cout << "> Error instantiating module!" << std::endl;
      exit(1);
    }

    // Extract export.
    auto exports = instance->exports();
    if (exports.size() == 0 || exports[0]->kind() != wasm::ExternKind::FUNC ||
        !exports[0]->func()) {
      std::lock_guard<std::mutex> lock(*mutex);
      std::cout << "> Error accessing export!" << std::endl;
      exit(1);
    }
    auto run_func = exports[0]->func();

    // Call.
    auto empty = wasm::vec<wasm::Val>::make();
    run_func->call(empty, empty);
  }
}

int main(int argc, const char *argv[]) {
  // Initialize.
  std::cout << "Initializing..." << std::endl;
  auto engine = wasm::Engine::make();

  // Load binary.
  std::cout << "Loading binary..." << std::endl;
  std::ifstream file("threads.wasm");
  file.seekg(0, std::ios_base::end);
  auto file_size = file.tellg();
  file.seekg(0);
  auto binary = wasm::vec<byte_t>::make_uninitialized(file_size);
  file.read(binary.get(), file_size);
  file.close();
  if (file.fail()) {
    std::cout << "> Error loading module!" << std::endl;
    return 1;
  }

  // Compile and share.
  std::cout << "Compiling and sharing module..." << std::endl;
  auto store = wasm::Store::make(engine.get());
  auto module = wasm::Module::make(store.get(), binary);
  auto shared = module->share();

  // Spawn threads.
  std::cout << "Spawning threads..." << std::endl;
  std::mutex mutex;
  std::thread threads[N_THREADS];
  for (int i = 0; i < N_THREADS; ++i) {
    {
      std::lock_guard<std::mutex> lock(mutex);
      std::cout << "Initializing thread " << i << "..." << std::endl;
    }
    threads[i] = std::thread(run, engine.get(), shared.get(), &mutex, i);
  }

  for (int i = 0; i < N_THREADS; ++i) {
    {
      std::lock_guard<std::mutex> lock(mutex);
      std::cout << "Waiting for thread " << i << "..." << std::endl;
    }
    threads[i].join();
  }

  return 0;
}
