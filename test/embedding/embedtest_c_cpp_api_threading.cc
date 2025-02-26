#include "embedtest_c_cpp_api_common.h"

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <functional>
#include <mutex>
#include <thread>

namespace node::embedding {

// Tests that multiple runtimes can be run at the same time in their own
// threads. The test creates 12 threads and 12 runtimes. Each runtime runs in it
// own thread.
int32_t test_main_c_cpp_api_threading_runtime_per_thread(int32_t argc,
                                                         const char* argv[]) {
  const size_t thread_count = 12;
  std::vector<std::thread> threads;
  threads.reserve(thread_count);
  std::atomic<int32_t> global_count{0};
  std::atomic<NodeStatus> global_status{NodeStatus::kOk};

  TestExitCodeHandler error_handler(argv[0]);
  {
    NodeExpected<NodePlatform> expected_platform =
        NodePlatform::Create(NodeArgs(argc, argv), nullptr);
    NODE_EMBEDDING_CALL(expected_platform.status());
    NodePlatform platform = std::move(expected_platform).value();
    if (!platform) {
      return error_handler.ReportResult();  // early return
    }

    for (size_t i = 0; i < thread_count; i++) {
      threads.emplace_back([&platform, &global_count, &global_status] {
        NodeExpected<void> result = NodeRuntime::Run(
            platform,
            [&](const NodePlatform& platform,
                const NodeRuntimeConfig& runtime_config) {
              NodeEmbeddingErrorHandler error_handler;
              // Inspector can be associated with only one
              // runtime in the process.
              NODE_EMBEDDING_CALL(runtime_config.SetFlags(
                  NodeRuntimeFlags::kDefault |
                  NodeRuntimeFlags::kNoCreateInspector));
              NODE_EMBEDDING_CALL(LoadUtf8Script(runtime_config, main_script));
              NODE_EMBEDDING_CALL(
                  runtime_config.OnLoaded([&](const NodeRuntime& runtime,
                                              napi_env env,
                                              napi_value /*value*/) {
                    NodeApiErrorHandler<void> error_handler(env);
                    napi_value global, my_count;
                    NODE_API_CALL(napi_get_global(env, &global));
                    NODE_API_CALL(napi_get_named_property(
                        env, global, "myCount", &my_count));
                    int32_t count;
                    NODE_API_CALL(napi_get_value_int32(env, my_count, &count));
                    global_count.fetch_add(count);
                  }));
              return error_handler.ReportResult();
            });
        if (result.has_error()) {
          global_status.store(result.status());
        }
      });
    }

    for (size_t i = 0; i < thread_count; i++) {
      threads[i].join();
    }

    NODE_EMBEDDING_CALL(NodeExpected<void>(global_status.load()));
  }

  fprintf(stdout, "%d\n", global_count.load());
  return error_handler.ReportResult();
}

// Tests that multiple runtimes can run in the same thread.
// The runtime scope must be opened and closed for each use.
// There are 12 runtimes that share the same main thread.
int32_t test_main_c_cpp_api_threading_several_runtimes_per_thread(
    int32_t argc, const char* argv[]) {
  const size_t runtime_count = 12;
  bool more_work = false;
  int32_t global_count = 0;

  TestExitCodeHandler error_handler(argv[0]);
  {
    NodeExpected<NodePlatform> expected_platform =
        NodePlatform::Create(NodeArgs(argc, argv), nullptr);
    NODE_EMBEDDING_CALL(expected_platform.status());
    NodePlatform platform = std::move(expected_platform).value();

    // We declared list of NodeRuntime after NodePlatform to ensure that they
    // are released before the platform.
    std::vector<NodeRuntime> runtimes;
    runtimes.reserve(runtime_count);

    for (size_t i = 0; i < runtime_count; i++) {
      NodeExpected<NodeRuntime> expected_runtime = NodeRuntime::Create(
          platform,
          [](const NodePlatform& platform,
             const NodeRuntimeConfig& runtime_config) {
            NodeEmbeddingErrorHandler error_handler;
            // Inspector can be associated with only one runtime in the process.
            NODE_EMBEDDING_CALL(
                runtime_config.SetFlags(NodeRuntimeFlags::kDefault |
                                        NodeRuntimeFlags::kNoCreateInspector));
            NODE_EMBEDDING_CALL(LoadUtf8Script(runtime_config, main_script));
            return error_handler.ReportResult();
          });

      NODE_EMBEDDING_CALL(expected_runtime.status());
      NodeRuntime runtime = std::move(expected_runtime).value();

      NODE_EMBEDDING_CALL(runtime.RunNodeApi([&](napi_env env) {
        NodeApiErrorHandler<void> error_handler(env);
        napi_value undefined, global, func;
        NODE_API_CALL(napi_get_undefined(env, &undefined));
        NODE_API_CALL(napi_get_global(env, &global));
        NODE_API_CALL(
            napi_get_named_property(env, global, "incMyCount", &func));

        napi_valuetype func_type;
        NODE_API_CALL(napi_typeof(env, func, &func_type));
        NODE_ASSERT(func_type == napi_function);
        NODE_API_CALL(
            napi_call_function(env, undefined, func, 0, nullptr, nullptr));
      }));

      runtimes.push_back(std::move(runtime));
    }

    do {
      more_work = false;
      for (const NodeRuntime& runtime : runtimes) {
        NodeExpected<bool> has_more_work = runtime.RunEventLoopNoWait();
        NODE_EMBEDDING_CALL(has_more_work.status());
        more_work |= has_more_work.value();
      }
    } while (more_work);

    for (const NodeRuntime& runtime : runtimes) {
      NODE_EMBEDDING_CALL(runtime.RunNodeApi([&](napi_env env) {
        NodeApiErrorHandler<void> error_handler(env);
        napi_value global, my_count;
        NODE_API_CALL(napi_get_global(env, &global));
        NODE_API_CALL(
            napi_get_named_property(env, global, "myCount", &my_count));

        napi_valuetype my_count_type;
        NODE_API_CALL(napi_typeof(env, my_count, &my_count_type));
        NODE_ASSERT(my_count_type == napi_number);
        int32_t count;
        NODE_API_CALL(napi_get_value_int32(env, my_count, &count));

        global_count += count;
      }));
      NODE_EMBEDDING_CALL(runtime.RunEventLoop());
    }
  }

  fprintf(stdout, "%d\n", global_count);
  return error_handler.ReportResult();
}

// Tests that a runtime can be invoked from different threads as long as only
// one thread uses it at a time.
int32_t test_main_c_cpp_api_threading_runtime_in_several_threads(
    int32_t argc, const char* argv[]) {
  // Use mutex to synchronize access to the runtime.
  std::mutex mutex;
  std::atomic<int32_t> result_count{0};
  std::atomic<NodeStatus> result_status{NodeStatus::kOk};
  const size_t thread_count = 5;
  std::vector<std::thread> threads;
  threads.reserve(thread_count);

  TestExitCodeHandler error_handler(argv[0]);
  {
    NodeExpected<NodePlatform> expected_platform =
        NodePlatform::Create(NodeArgs(argc, argv), nullptr);
    NODE_EMBEDDING_CALL(expected_platform.status());
    NodePlatform platform = std::move(expected_platform).value();
    if (!platform) {
      return error_handler.ReportResult();  // early return
    }

    NodeExpected<NodeRuntime> expected_runtime = NodeRuntime::Create(
        platform,
        [](const NodePlatform& platform,
           const NodeRuntimeConfig& runtime_config) {
          return LoadUtf8Script(runtime_config, main_script);
        });

    NODE_EMBEDDING_CALL(expected_runtime.status());
    NodeRuntime runtime = std::move(expected_runtime).value();

    for (size_t i = 0; i < thread_count; i++) {
      threads.emplace_back([&runtime, &result_count, &result_status, &mutex] {
        std::scoped_lock lock(mutex);
        NodeExpected<void> run_result = runtime.RunNodeApi([&](napi_env env) {
          NodeApiErrorHandler<void> error_handler(env);
          napi_value undefined, global, func, my_count;
          NODE_API_CALL(napi_get_undefined(env, &undefined));
          NODE_API_CALL(napi_get_global(env, &global));
          NODE_API_CALL(
              napi_get_named_property(env, global, "incMyCount", &func));

          napi_valuetype func_type;
          NODE_API_CALL(napi_typeof(env, func, &func_type));
          NODE_ASSERT(func_type == napi_function);
          NODE_API_CALL(
              napi_call_function(env, undefined, func, 0, nullptr, nullptr));

          NODE_API_CALL(
              napi_get_named_property(env, global, "myCount", &my_count));
          napi_valuetype count_type;
          NODE_API_CALL(napi_typeof(env, my_count, &count_type));
          NODE_ASSERT(count_type == napi_number);
          int32_t count;
          NODE_API_CALL(napi_get_value_int32(env, my_count, &count));
          result_count.store(count);
        });
        if (run_result.has_error()) {
          result_status.store(run_result.status());
        }
      });
    }

    for (size_t i = 0; i < thread_count; i++) {
      threads[i].join();
    }

    NODE_EMBEDDING_CALL(NodeExpected<void>(result_status.load()));
    NODE_EMBEDDING_CALL(runtime.RunEventLoop());
  }

  fprintf(stdout, "%d\n", result_count.load());
  return error_handler.ReportResult();
}

namespace {

// A simulation of the UI thread's event loop implemented as a dispatcher
// queue. Note that it is a very simplistic implementation not suitable
// for the real apps.
class UIQueue {
 public:
  void PostTask(std::function<void()>&& task) {
    std::scoped_lock lock(mutex_);
    if (!is_finished_) {
      tasks_.push_back(std::move(task));
      wakeup_.notify_one();
    }
  }

  void Run() {
    for (;;) {
      // Invoke task outside of the lock.
      std::function<void()> task;
      {
        std::unique_lock lock(mutex_);
        wakeup_.wait(lock, [&] { return is_finished_ || !tasks_.empty(); });
        if (is_finished_) return;
        task = std::move(tasks_.front());
        tasks_.pop_front();
      }
      task();
    }
  }

  void Stop() {
    std::scoped_lock lock(mutex_);
    if (!is_finished_) {
      is_finished_ = true;
      wakeup_.notify_one();
    }
  }

 private:
  std::mutex mutex_;
  std::condition_variable wakeup_;
  std::deque<std::function<void()>> tasks_;
  bool is_finished_{false};
};

}  // namespace

// Tests that a the runtime's event loop can be called from the UI thread
// event loop.
int32_t test_main_c_cpp_api_threading_runtime_in_ui_thread(int32_t argc,
                                                           const char* argv[]) {
  UIQueue ui_queue;
  const char* exe_name = argv[0];
  TestExitCodeHandler error_handler(exe_name);
  {
    NodeExpected<NodePlatform> expected_platform =
        NodePlatform::Create(NodeArgs(argc, argv), nullptr);
    NODE_EMBEDDING_CALL(expected_platform.status());
    NodePlatform platform = std::move(expected_platform).value();
    if (!platform) {
      return error_handler.ReportResult();  // early return
    }

    NodeRuntime runtime{nullptr};
    NodeExpected<NodeRuntime> expected_runtime = NodeRuntime::Create(
        platform,
        [&](const NodePlatform& platform,
            const NodeRuntimeConfig& runtime_config) {
          NodeEmbeddingErrorHandler error_handler;
          // The callback will be invoked from the runtime's event loop
          // observer thread. It must schedule the work to the UI thread's
          // event loop.
          NODE_EMBEDDING_CALL(runtime_config.SetTaskRunner(
              // We capture the ui_queue by reference here because we
              // guarantee it to be alive till the end of the test. In
              // real applications, you should use a safer way to
              // capture the dispatcher queue.
              [&ui_queue, &runtime, exe_name](NodeRunTaskCallback run_task) {
                // TODO: figure out the termination scenario.
                // TODO: Release run_task data.
                ui_queue.PostTask([run_task =
                                       std::make_shared<NodeRunTaskCallback>(
                                           std::move(run_task)),
                                   &runtime,
                                   &ui_queue,
                                   exe_name]() {
                  TestExitOnErrorHandler error_handler(exe_name);
                  NODE_EMBEDDING_CALL((*run_task)());
                  // Check myCount and stop the processing when it reaches 5.
                  int32_t count{};
                  NODE_EMBEDDING_CALL(runtime.RunNodeApi([&](napi_env env) {
                    NodeApiErrorHandler<void> error_handler(env);
                    napi_value global, my_count;
                    NODE_API_CALL(napi_get_global(env, &global));
                    NODE_API_CALL(napi_get_named_property(
                        env, global, "myCount", &my_count));
                    napi_valuetype count_type;
                    NODE_API_CALL(napi_typeof(env, my_count, &count_type));
                    NODE_ASSERT(count_type == napi_number);
                    NODE_API_CALL(napi_get_value_int32(env, my_count, &count));
                  }));
                  if (count == 5) {
                    NODE_EMBEDDING_CALL(runtime.RunEventLoop());
                    fprintf(stdout, "%d\n", count);
                    ui_queue.Stop();
                  }
                });
                return NodeExpected<bool>(true);
              }));

          NODE_EMBEDDING_CALL(LoadUtf8Script(runtime_config, main_script));
          return error_handler.ReportResult();
        });
    NODE_EMBEDDING_CALL(expected_runtime.status());
    runtime = std::move(expected_runtime).value();

    // The initial task starts the JS code that then will do the timer
    // scheduling. The timer supposed to be handled by the runtime's event loop.
    ui_queue.PostTask([&runtime, exe_name]() {
      TestExitOnErrorHandler error_handler(exe_name);
      NODE_EMBEDDING_CALL(runtime.RunNodeApi([&](napi_env env) {
        NodeApiErrorHandler<void> error_handler(env);
        napi_value undefined, global, func;
        NODE_API_CALL(napi_get_undefined(env, &undefined));
        NODE_API_CALL(napi_get_global(env, &global));
        NODE_API_CALL(
            napi_get_named_property(env, global, "incMyCount", &func));

        napi_valuetype func_type;
        NODE_API_CALL(napi_typeof(env, func, &func_type));
        NODE_ASSERT(func_type == napi_function);
        NODE_API_CALL(
            napi_call_function(env, undefined, func, 0, nullptr, nullptr));
      }));
    });

    ui_queue.Run();
  }

  return error_handler.ReportResult();
}

}  // namespace node::embedding
