#include "embedtest_c_api_common.h"

#include <condition_variable>
#include <deque>
#include <mutex>
#include <thread>

using namespace node;

// Tests that multiple runtimes can be run at the same time in their own
// threads. The test creates 12 threads and 12 runtimes. Each runtime runs in it
// own thread.
extern "C" int32_t test_main_threading_runtime_per_thread_node_api(
    int32_t argc, char* argv[]) {
  node_embedding_platform platform;
  const size_t thread_count = 12;
  std::vector<std::thread> threads;
  threads.reserve(thread_count);
  std::atomic<int32_t> global_count{0};
  std::atomic<node_embedding_status> global_status{};

  CHECK_STATUS_OR_EXIT(
      node_embedding_create_platform(argc, argv, {}, &platform));
  if (!platform) {
    return 0;  // early return
  }

  for (size_t i = 0; i < thread_count; i++) {
    threads.emplace_back([platform, &global_count, &global_status] {
      node_embedding_status status = [&]() {
        CHECK_STATUS(node_embedding_run_runtime(
            platform,
            AsFunctorRef<node_embedding_configure_runtime_functor_ref>(
                [&](node_embedding_platform platform,
                    node_embedding_runtime_config runtime_config) {
                  // Inspector can be associated with only one runtime in the
                  // process.
                  CHECK_STATUS(node_embedding_runtime_set_flags(
                      runtime_config,
                      node_embedding_runtime_flags_default |
                          node_embedding_runtime_flags_no_create_inspector));
                  CHECK_STATUS(LoadUtf8Script(
                      runtime_config,
                      main_script,
                      AsFunctor<node_embedding_handle_result_functor>(
                          [&](node_embedding_runtime runtime,
                              napi_env env,
                              napi_value /*value*/) {
                            napi_value global, my_count;
                            NODE_API_CALL_RETURN_VOID(
                                napi_get_global(env, &global));
                            NODE_API_CALL_RETURN_VOID(napi_get_named_property(
                                env, global, "myCount", &my_count));
                            int32_t count;
                            NODE_API_CALL_RETURN_VOID(
                                napi_get_value_int32(env, my_count, &count));
                            global_count.fetch_add(count);
                          })));
                  return node_embedding_status_ok;
                })));
        return node_embedding_status_ok;
      }();
      if (status != node_embedding_status_ok) {
        global_status.store(status);
      }
    });
  }

  for (size_t i = 0; i < thread_count; i++) {
    threads[i].join();
  }

  CHECK_STATUS_OR_EXIT(global_status.load());

  CHECK_STATUS_OR_EXIT(node_embedding_delete_platform(platform));

  fprintf(stdout, "%d\n", global_count.load());

  return 0;
}

// Tests that multiple runtimes can run in the same thread.
// The runtime scope must be opened and closed for each use.
// There are 12 runtimes that share the same main thread.
extern "C" int32_t test_main_threading_several_runtimes_per_thread_node_api(
    int32_t argc, char* argv[]) {
  node_embedding_platform platform;
  const size_t runtime_count = 12;
  std::vector<node_embedding_runtime> runtimes;
  runtimes.reserve(runtime_count);
  bool more_work = false;
  int32_t global_count = 0;

  CHECK_STATUS_OR_EXIT(
      node_embedding_create_platform(argc, argv, {}, &platform));
  if (!platform) {
    return 0;  // early return
  }

  for (size_t i = 0; i < runtime_count; i++) {
    node_embedding_runtime runtime;
    CHECK_STATUS_OR_EXIT(node_embedding_create_runtime(
        platform,
        AsFunctorRef<node_embedding_configure_runtime_functor_ref>(
            [&](node_embedding_platform platform,
                node_embedding_runtime_config runtime_config) {
              // Inspector can be associated with only one runtime in the
              // process.
              CHECK_STATUS(node_embedding_runtime_set_flags(
                  runtime_config,
                  node_embedding_runtime_flags_default |
                      node_embedding_runtime_flags_no_create_inspector));
              CHECK_STATUS(LoadUtf8Script(runtime_config, main_script));
              return node_embedding_status_ok;
            }),
        &runtime));
    runtimes.push_back(runtime);

    CHECK_STATUS_OR_EXIT(node_embedding_run_node_api(
        runtime,
        AsFunctorRef<node_embedding_run_node_api_functor_ref>(
            [&](node_embedding_runtime runtime, napi_env env) {
              napi_value undefined, global, func;
              NODE_API_CALL_RETURN_VOID(napi_get_undefined(env, &undefined));
              NODE_API_CALL_RETURN_VOID(napi_get_global(env, &global));
              NODE_API_CALL_RETURN_VOID(
                  napi_get_named_property(env, global, "incMyCount", &func));

              napi_valuetype func_type;
              NODE_API_CALL_RETURN_VOID(napi_typeof(env, func, &func_type));
              NODE_API_ASSERT_RETURN_VOID(func_type == napi_function);
              NODE_API_CALL_RETURN_VOID(napi_call_function(
                  env, undefined, func, 0, nullptr, nullptr));
            })));
  }

  do {
    more_work = false;
    for (node_embedding_runtime runtime : runtimes) {
      bool has_more_work = false;
      CHECK_STATUS_OR_EXIT(node_embedding_run_event_loop(
          runtime, node_embedding_event_loop_run_mode_nowait, &has_more_work));
      more_work |= has_more_work;
    }
  } while (more_work);

  for (node_embedding_runtime runtime : runtimes) {
    CHECK_STATUS_OR_EXIT(node_embedding_run_node_api(
        runtime,
        AsFunctorRef<node_embedding_run_node_api_functor_ref>(
            [&](node_embedding_runtime runtime, napi_env env) {
              napi_value global, my_count;
              NODE_API_CALL_RETURN_VOID(napi_get_global(env, &global));
              NODE_API_CALL_RETURN_VOID(
                  napi_get_named_property(env, global, "myCount", &my_count));

              napi_valuetype my_count_type;
              NODE_API_CALL_RETURN_VOID(
                  napi_typeof(env, my_count, &my_count_type));
              NODE_API_ASSERT_RETURN_VOID(my_count_type == napi_number);
              int32_t count;
              NODE_API_CALL_RETURN_VOID(
                  napi_get_value_int32(env, my_count, &count));

              global_count += count;
            })));
    CHECK_STATUS_OR_EXIT(node_embedding_complete_event_loop(runtime));
    CHECK_STATUS_OR_EXIT(node_embedding_delete_runtime(runtime));
  }

  CHECK_STATUS_OR_EXIT(node_embedding_delete_platform(platform));

  fprintf(stdout, "%d\n", global_count);

  return 0;
}

// Tests that a runtime can be invoked from different threads as long as only
// one thread uses it at a time.
extern "C" int32_t test_main_threading_runtime_in_several_threads_node_api(
    int32_t argc, char* argv[]) {
  node_embedding_platform platform;

  // Use mutex to synchronize access to the runtime.
  std::mutex mutex;
  std::atomic<int32_t> result_count{0};
  std::atomic<node_embedding_status> result_status{node_embedding_status_ok};
  const size_t thread_count = 5;
  std::vector<std::thread> threads;
  threads.reserve(thread_count);

  CHECK_STATUS_OR_EXIT(
      node_embedding_create_platform(argc, argv, {}, &platform));
  if (!platform) {
    return 0;  // early return
  }

  node_embedding_runtime runtime;
  CHECK_STATUS_OR_EXIT(node_embedding_create_runtime(
      platform,
      AsFunctorRef<node_embedding_configure_runtime_functor_ref>(
          [&](node_embedding_platform platform,
              node_embedding_runtime_config runtime_config) {
            CHECK_STATUS(LoadUtf8Script(runtime_config, main_script));
            return node_embedding_status_ok;
          }),
      &runtime));

  for (size_t i = 0; i < thread_count; i++) {
    threads.emplace_back([runtime, &result_count, &result_status, &mutex] {
      std::scoped_lock lock(mutex);
      node_embedding_status status = node_embedding_run_node_api(
          runtime,
          AsFunctorRef<node_embedding_run_node_api_functor_ref>(
              [&](node_embedding_runtime runtime, napi_env env) {
                napi_value undefined, global, func, my_count;
                NODE_API_CALL_RETURN_VOID(napi_get_undefined(env, &undefined));
                NODE_API_CALL_RETURN_VOID(napi_get_global(env, &global));
                NODE_API_CALL_RETURN_VOID(
                    napi_get_named_property(env, global, "incMyCount", &func));

                napi_valuetype func_type;
                NODE_API_CALL_RETURN_VOID(napi_typeof(env, func, &func_type));
                NODE_API_ASSERT_RETURN_VOID(func_type == napi_function);
                NODE_API_CALL_RETURN_VOID(napi_call_function(
                    env, undefined, func, 0, nullptr, nullptr));

                NODE_API_CALL_RETURN_VOID(
                    napi_get_named_property(env, global, "myCount", &my_count));
                napi_valuetype count_type;
                NODE_API_CALL_RETURN_VOID(
                    napi_typeof(env, my_count, &count_type));
                NODE_API_ASSERT_RETURN_VOID(count_type == napi_number);
                int32_t count;
                NODE_API_CALL_RETURN_VOID(
                    napi_get_value_int32(env, my_count, &count));
                result_count.store(count);
              }));
      if (status != node_embedding_status_ok) {
        result_status.store(status);
      }
    });
  }

  for (size_t i = 0; i < thread_count; i++) {
    threads[i].join();
  }

  CHECK_STATUS_OR_EXIT(result_status.load());

  CHECK_STATUS_OR_EXIT(node_embedding_complete_event_loop(runtime));
  CHECK_STATUS_OR_EXIT(node_embedding_delete_runtime(runtime));
  CHECK_STATUS_OR_EXIT(node_embedding_delete_platform(platform));

  fprintf(stdout, "%d\n", result_count.load());

  return 0;
}

// Tests that a the runtime's event loop can be called from the UI thread
// event loop.
extern "C" int32_t test_main_threading_runtime_in_ui_thread_node_api(
    int32_t argc, char* argv[]) {
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
        std::function<void()> task;
        {
          std::unique_lock lock(mutex_);
          wakeup_.wait(lock, [&] { return is_finished_ || !tasks_.empty(); });
          if (is_finished_) break;
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
  } ui_queue;

  node_embedding_platform platform;
  CHECK_STATUS_OR_EXIT(
      node_embedding_create_platform(argc, argv, {}, &platform));
  if (!platform) {
    return 0;  // early return
  }

  node_embedding_runtime runtime;
  CHECK_STATUS_OR_EXIT(node_embedding_create_runtime(
      platform,
      AsFunctorRef<node_embedding_configure_runtime_functor_ref>(
          [&](node_embedding_platform platform,
              node_embedding_runtime_config runtime_config) {
            // The callback will be invoked from the runtime's event loop
            // observer thread. It must schedule the work to the UI thread's
            // event loop.
            CHECK_STATUS(node_embedding_runtime_set_task_runner(
                runtime_config,
                AsFunctor<node_embedding_post_task_functor>(
                    // We capture the ui_queue by reference here because we
                    // guarantee it to be alive till the end of the test. In
                    // real applications, you should use a safer way to capture
                    // the dispatcher queue.
                    [&ui_queue,
                     &runtime](node_embedding_run_task_functor run_task) {
                      // TODO: figure out the termination scenario.
                      ui_queue.PostTask([run_task, &runtime, &ui_queue]() {
                        AsStdFunction(run_task)();

                        // Check myCount and stop the processing when it
                        // reaches 5.
                        CHECK_STATUS_OR_EXIT(node_embedding_run_node_api(
                            runtime,
                            AsFunctorRef<
                                node_embedding_run_node_api_functor_ref>(
                                [&](node_embedding_runtime runtime,
                                    napi_env env) {
                                  napi_value global, my_count;
                                  NODE_API_CALL_RETURN_VOID(
                                      napi_get_global(env, &global));
                                  NODE_API_CALL_RETURN_VOID(
                                      napi_get_named_property(
                                          env, global, "myCount", &my_count));
                                  napi_valuetype count_type;
                                  NODE_API_CALL_RETURN_VOID(
                                      napi_typeof(env, my_count, &count_type));
                                  NODE_API_ASSERT_RETURN_VOID(count_type ==
                                                              napi_number);
                                  int32_t count;
                                  NODE_API_CALL_RETURN_VOID(
                                      napi_get_value_int32(
                                          env, my_count, &count));
                                  if (count == 5) {
                                    node_embedding_complete_event_loop(runtime);
                                    fprintf(stdout, "%d\n", count);
                                    ui_queue.Stop();
                                  }
                                })));
                      });
                    })));

            CHECK_STATUS(LoadUtf8Script(runtime_config, main_script));

            return node_embedding_status_ok;
          }),
      &runtime));

  // The initial task starts the JS code that then will do the timer
  // scheduling. The timer supposed to be handled by the runtime's event loop.
  ui_queue.PostTask([runtime]() {
    node_embedding_status status = node_embedding_run_node_api(
        runtime,
        AsFunctorRef<node_embedding_run_node_api_functor_ref>(
            [&](node_embedding_runtime runtime, napi_env env) {
              napi_value undefined, global, func;
              NODE_API_CALL_RETURN_VOID(napi_get_undefined(env, &undefined));
              NODE_API_CALL_RETURN_VOID(napi_get_global(env, &global));
              NODE_API_CALL_RETURN_VOID(
                  napi_get_named_property(env, global, "incMyCount", &func));

              napi_valuetype func_type;
              NODE_API_CALL_RETURN_VOID(napi_typeof(env, func, &func_type));
              NODE_API_ASSERT_RETURN_VOID(func_type == napi_function);
              NODE_API_CALL_RETURN_VOID(napi_call_function(
                  env, undefined, func, 0, nullptr, nullptr));

              node_embedding_run_event_loop(
                  runtime, node_embedding_event_loop_run_mode_nowait, nullptr);
            }));
    CHECK_STATUS_OR_EXIT(status);
  });

  ui_queue.Run();

  CHECK_STATUS_OR_EXIT(node_embedding_delete_runtime(runtime));
  CHECK_STATUS_OR_EXIT(node_embedding_delete_platform(platform));

  return 0;
}
