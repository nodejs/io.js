#include "embedtest_c_api_common.h"

#include <uv.h>  // Tests in this file use libuv for threading.

//==============================================================================
// Test that multiple runtimes can be run at the same time in their own threads.
//==============================================================================

typedef struct {
  node_embedding_platform platform;
  uv_mutex_t mutex;
  int32_t global_count;
  node_embedding_status global_status;
} test_data1_t;

static void OnLoaded1(void* cb_data,
                      node_embedding_runtime runtime,
                      napi_env env,
                      napi_value execution_result) {
  napi_status status = napi_ok;
  test_data1_t* data = (test_data1_t*)cb_data;
  napi_value global, my_count;
  NODE_API_CALL(napi_get_global(env, &global));
  NODE_API_CALL(napi_get_named_property(env, global, "myCount", &my_count));
  int32_t count;
  NODE_API_CALL(napi_get_value_int32(env, my_count, &count));
  uv_mutex_lock(&data->mutex);
  ++data->global_count;
  uv_mutex_unlock(&data->mutex);
on_exit:
  GetAndThrowLastErrorMessage(env, status);
}

static node_embedding_status ConfigureRuntime(
    void* cb_data,
    node_embedding_platform platform,
    node_embedding_runtime_config runtime_config) {
  node_embedding_status embedding_status = node_embedding_status_ok;
  // Inspector can be associated with only one
  // runtime in the process.
  NODE_EMBEDDING_CALL(node_embedding_runtime_config_set_flags(
      runtime_config,
      node_embedding_runtime_flags_default |
          node_embedding_runtime_flags_no_create_inspector));
  NODE_EMBEDDING_CALL(LoadUtf8Script(runtime_config, main_script));
  NODE_EMBEDDING_CALL(node_embedding_runtime_config_on_loaded(
      runtime_config, OnLoaded1, cb_data, NULL));
on_exit:
  return embedding_status;
}

static void ThreadCallback1(void* arg) {
  test_data1_t* data = (test_data1_t*)arg;
  node_embedding_status status =
      node_embedding_runtime_run(data->platform, ConfigureRuntime, arg);
  if (status != node_embedding_status_ok) {
    uv_mutex_lock(&data->mutex);
    data->global_status = status;
    uv_mutex_unlock(&data->mutex);
  }
}

#define TEST_THREAD_COUNT1 12

// Tests that multiple runtimes can be run at the same time in their own
// threads. The test creates 12 threads and 12 runtimes. Each runtime runs in it
// own thread.
int32_t test_main_c_api_threading_runtime_per_thread(int32_t argc,
                                                     const char* argv[]) {
  node_embedding_status embedding_status = node_embedding_status_ok;
  size_t thread_count = TEST_THREAD_COUNT1;
  uv_thread_t threads[TEST_THREAD_COUNT1] = {0};
  test_data1_t test_data = {0};
  uv_mutex_init(&test_data.mutex);

  NODE_EMBEDDING_CALL(node_embedding_platform_create(
      NODE_EMBEDDING_VERSION, argc, argv, NULL, NULL, &test_data.platform));
  if (test_data.platform == NULL) {
    goto on_exit;  // early return
  }

  for (size_t i = 0; i < thread_count; i++) {
    uv_thread_create(&threads[i], ThreadCallback1, &test_data);
  }

  for (size_t i = 0; i < thread_count; i++) {
    uv_thread_join(&threads[i]);
  }

  // TODO: Add passing error message
  NODE_EMBEDDING_CALL(test_data.global_status);
  NODE_EMBEDDING_CALL(node_embedding_platform_delete(test_data.platform));

  fprintf(stdout, "%d\n", test_data.global_count);

on_exit:
  uv_mutex_destroy(&test_data.mutex);
  return StatusToExitCode(PrintErrorMessage(argv[0], embedding_status));
}

//==============================================================================
// Test that multiple runtimes can run in the same thread.
//==============================================================================

static node_embedding_status ConfigureRuntime2(
    void* cb_data,
    node_embedding_platform platform,
    node_embedding_runtime_config runtime_config) {
  node_embedding_status embedding_status = node_embedding_status_ok;
  // Inspector can be associated with only one runtime in the process.
  NODE_EMBEDDING_CALL(node_embedding_runtime_config_set_flags(
      runtime_config,
      node_embedding_runtime_flags_default |
          node_embedding_runtime_flags_no_create_inspector));
  NODE_EMBEDDING_CALL(LoadUtf8Script(runtime_config, main_script));
on_exit:
  return embedding_status;
}

static void IncMyCount2(void* cb_data, napi_env env) {
  napi_status status = napi_ok;
  napi_value undefined, global, func;
  NODE_API_CALL(napi_get_undefined(env, &undefined));
  NODE_API_CALL(napi_get_global(env, &global));
  NODE_API_CALL(napi_get_named_property(env, global, "incMyCount", &func));

  napi_valuetype func_type;
  NODE_API_CALL(napi_typeof(env, func, &func_type));
  NODE_API_ASSERT(func_type == napi_function);
  NODE_API_CALL(napi_call_function(env, undefined, func, 0, NULL, NULL));
on_exit:
  GetAndThrowLastErrorMessage(env, status);
}

static void SumMyCount2(void* cb_data, napi_env env) {
  napi_status status = napi_ok;
  int32_t* global_count = (int32_t*)cb_data;
  napi_value global, my_count;
  NODE_API_CALL(napi_get_global(env, &global));
  NODE_API_CALL(napi_get_named_property(env, global, "myCount", &my_count));

  napi_valuetype my_count_type;
  NODE_API_CALL(napi_typeof(env, my_count, &my_count_type));
  NODE_API_ASSERT(my_count_type == napi_number);
  int32_t count;
  NODE_API_CALL(napi_get_value_int32(env, my_count, &count));

  *global_count += count;
on_exit:
  GetAndThrowLastErrorMessage(env, status);
}

// Tests that multiple runtimes can run in the same thread.
// The runtime scope must be opened and closed for each use.
// There are 12 runtimes that share the same main thread.
int32_t test_main_c_api_threading_several_runtimes_per_thread(
    int32_t argc, const char* argv[]) {
  node_embedding_status embedding_status = node_embedding_status_ok;
  const size_t runtime_count = 12;
  bool more_work = false;
  int32_t global_count = 0;
  node_embedding_runtime runtimes[12] = {0};

  node_embedding_platform platform;
  NODE_EMBEDDING_CALL(node_embedding_platform_create(
      NODE_EMBEDDING_VERSION, argc, argv, NULL, NULL, &platform));
  if (platform == NULL) {
    goto on_exit;  // early return
  }

  for (size_t i = 0; i < runtime_count; ++i) {
    NODE_EMBEDDING_CALL(node_embedding_runtime_create(
        platform, ConfigureRuntime2, NULL, &runtimes[i]));

    NODE_EMBEDDING_CALL(
        node_embedding_runtime_node_api_run(runtimes[i], IncMyCount2, NULL));
  }

  do {
    more_work = false;
    for (size_t i = 0; i < runtime_count; ++i) {
      bool has_more_work = false;
      NODE_EMBEDDING_CALL(node_embedding_runtime_event_loop_run_no_wait(
          runtimes[i], &has_more_work));
      more_work |= has_more_work;
    }
  } while (more_work);

  for (size_t i = 0; i < runtime_count; ++i) {
    NODE_EMBEDDING_CALL(node_embedding_runtime_node_api_run(
        runtimes[i], SumMyCount2, &global_count));
    NODE_EMBEDDING_CALL(node_embedding_runtime_event_loop_run(runtimes[i]));
    NODE_EMBEDDING_CALL(node_embedding_runtime_delete(runtimes[i]));
  }

  NODE_EMBEDDING_CALL(node_embedding_platform_delete(platform));

  fprintf(stdout, "%d\n", global_count);
on_exit:
  return StatusToExitCode(PrintErrorMessage(argv[0], embedding_status));
}

//==============================================================================
// Test that a runtime can be invoked from different threads as long as only
// one thread uses it at a time.
//==============================================================================

typedef struct {
  node_embedding_runtime runtime;
  uv_mutex_t mutex;
  int32_t result_count;
  node_embedding_status result_status;
} thread_data3;

static node_embedding_status ConfigureRuntime3(
    void* cb_data,
    node_embedding_platform platform,
    node_embedding_runtime_config runtime_config) {
  return LoadUtf8Script(runtime_config, main_script);
}

static void RunNodeApi3(void* cb_data, napi_env env) {
  napi_status status = napi_ok;
  thread_data3* data = (thread_data3*)cb_data;
  napi_value undefined, global, func, my_count;
  NODE_API_CALL(napi_get_undefined(env, &undefined));
  NODE_API_CALL(napi_get_global(env, &global));
  NODE_API_CALL(napi_get_named_property(env, global, "incMyCount", &func));

  napi_valuetype func_type;
  NODE_API_CALL(napi_typeof(env, func, &func_type));
  NODE_API_ASSERT(func_type == napi_function);
  NODE_API_CALL(napi_call_function(env, undefined, func, 0, NULL, NULL));

  NODE_API_CALL(napi_get_named_property(env, global, "myCount", &my_count));
  napi_valuetype count_type;
  NODE_API_CALL(napi_typeof(env, my_count, &count_type));
  NODE_API_ASSERT(count_type == napi_number);
  int32_t count;
  NODE_API_CALL(napi_get_value_int32(env, my_count, &count));
  data->result_count = count;
on_exit:
  GetAndThrowLastErrorMessage(env, status);
}

static void ThreadCallback3(void* arg) {
  thread_data3* data = (thread_data3*)arg;
  uv_mutex_lock(&data->mutex);
  node_embedding_status status =
      node_embedding_runtime_node_api_run(data->runtime, RunNodeApi3, arg);
  if (status != node_embedding_status_ok) {
    data->result_status = status;
  }
  uv_mutex_unlock(&data->mutex);
}

// Tests that a runtime can be invoked from different threads as long as only
// one thread uses it at a time.
int32_t test_main_c_api_threading_runtime_in_several_threads(
    int32_t argc, const char* argv[]) {
  // Use mutex to synchronize access to the runtime.
  node_embedding_status embedding_status = node_embedding_status_ok;
  thread_data3 data = {0};
  uv_mutex_init(&data.mutex);

  const size_t thread_count = 5;
  uv_thread_t threads[5] = {0};

  node_embedding_platform platform;
  NODE_EMBEDDING_CALL(node_embedding_platform_create(
      NODE_EMBEDDING_VERSION, argc, argv, NULL, NULL, &platform));
  if (platform == NULL) {
    goto on_exit;  // early return
  }

  NODE_EMBEDDING_CALL(node_embedding_runtime_create(
      platform, ConfigureRuntime3, NULL, &data.runtime));

  for (size_t i = 0; i < thread_count; ++i) {
    uv_thread_create(&threads[i], ThreadCallback3, &data);
  }

  for (size_t i = 0; i < thread_count; ++i) {
    uv_thread_join(&threads[i]);
  }

  NODE_EMBEDDING_CALL(data.result_status);
  NODE_EMBEDDING_CALL(node_embedding_runtime_event_loop_run(data.runtime));

  fprintf(stdout, "%d\n", data.result_count);
on_exit:
  uv_mutex_destroy(&data.mutex);
  return StatusToExitCode(PrintErrorMessage(argv[0], embedding_status));
}

//==============================================================================
// Test that a runtime's event loop can be called from the UI thread event loop.
//==============================================================================

//------------------------------------------------------------------------------
// Simulation of the UI queue.
//------------------------------------------------------------------------------

struct test_deq_item_t {
  struct test_deq_item_t* prev;
  struct test_deq_item_t* next;
};
typedef struct test_deq_item_t test_deq_item_t;

static void test_deq_item_init(test_deq_item_t* item) {
  item->prev = NULL;
  item->next = NULL;
}

typedef struct {
  test_deq_item_t* head;
  test_deq_item_t* tail;
} test_deq_t;

static void test_deq_init(test_deq_t* deq) {
  deq->head = NULL;
  deq->tail = NULL;
}

static void test_deq_push_back(test_deq_t* deq, test_deq_item_t* item) {
  if (deq->tail == NULL) {
    deq->head = item;
    deq->tail = item;
  } else {
    deq->tail->next = item;
    item->prev = deq->tail;
    deq->tail = item;
  }
}

static test_deq_item_t* test_deq_pop_front(test_deq_t* deq) {
  test_deq_item_t* item = deq->head;
  if (item != NULL) {
    deq->head = item->next;
    if (deq->head == NULL) {
      deq->tail = NULL;
    } else {
      deq->head->prev = NULL;
    }
  }
  return item;
}

static bool test_deq_empty(test_deq_t* deq) {
  return deq->head == NULL;
}

typedef struct {
  test_deq_item_t deq_item;
  void* task_data;
  void (*run_task)(void*);
  void (*release_task_data)(void*);
} test_task_t;

static void test_task_init(test_task_t* task,
                           void* task_data,
                           void (*run_task)(void*),
                           void (*release_task_data)(void*)) {
  test_deq_item_init(&task->deq_item);
  task->task_data = task_data;
  task->run_task = run_task;
  task->release_task_data = release_task_data;
}

typedef struct {
  uv_mutex_t mutex;
  uv_cond_t wakeup;
  test_deq_t tasks;
  bool is_finished;
} test_ui_queue_t;

static void test_ui_queue_init(test_ui_queue_t* queue) {
  uv_mutex_init(&queue->mutex);
  uv_cond_init(&queue->wakeup);
  test_deq_init(&queue->tasks);
  queue->is_finished = false;
}

static void test_ui_queue_post_task(test_ui_queue_t* queue, test_task_t* task) {
  uv_mutex_lock(&queue->mutex);
  if (!queue->is_finished) {
    test_deq_push_back(&queue->tasks, &task->deq_item);
    uv_cond_signal(&queue->wakeup);
  }
  uv_mutex_unlock(&queue->mutex);
}

static void test_ui_queue_run(test_ui_queue_t* queue) {
  for (;;) {
    uv_mutex_lock(&queue->mutex);
    while (!queue->is_finished && test_deq_empty(&queue->tasks)) {
      uv_cond_wait(&queue->wakeup, &queue->mutex);
    }
    if (queue->is_finished) {
      uv_mutex_unlock(&queue->mutex);
      return;
    }
    test_task_t* task = (test_task_t*)test_deq_pop_front(&queue->tasks);
    uv_mutex_unlock(&queue->mutex);
    if (task->run_task != NULL) {
      task->run_task(task->task_data);
    }
    if (task->release_task_data != NULL) {
      task->release_task_data(task->task_data);
    }
  }
}

static void test_ui_queue_stop(test_ui_queue_t* queue) {
  uv_mutex_lock(&queue->mutex);
  if (!queue->is_finished) {
    queue->is_finished = true;
    uv_cond_signal(&queue->wakeup);
  }
  uv_mutex_unlock(&queue->mutex);
}

static void test_ui_queue_destroy(test_ui_queue_t* queue) {
  uv_mutex_destroy(&queue->mutex);
  uv_cond_destroy(&queue->wakeup);
}

//------------------------------------------------------------------------------
// Test data and task implementation.
//------------------------------------------------------------------------------

typedef struct {
  test_ui_queue_t ui_queue;
  node_embedding_runtime runtime;
} test_data4_t;

typedef struct {
  test_task_t parent_task;
  node_embedding_task_run_callback run_task;
  void* task_data;
  node_embedding_data_release_callback release_task_data;
  test_data4_t* test_data;
} test_ext_task_t;

static void RunTestTask4(void* cb_data) {
  node_embedding_status embedding_status = node_embedding_status_ok;
  napi_status status = napi_ok;
  test_ext_task_t* test_task = (test_ext_task_t*)cb_data;

  test_task->run_task(test_task->task_data);  // TODO: handle result

  // Check myCount and stop the processing when it reaches 5.
  int32_t count;
  node_embedding_runtime runtime = test_task->test_data->runtime;
  node_embedding_node_api_scope node_api_scope;
  napi_env env;
  NODE_EMBEDDING_CALL(node_embedding_runtime_node_api_scope_open(
      runtime, &node_api_scope, &env));

  napi_value global, my_count;
  NODE_API_CALL(napi_get_global(env, &global));
  NODE_API_CALL(napi_get_named_property(env, global, "myCount", &my_count));
  napi_valuetype count_type;
  NODE_API_CALL(napi_typeof(env, my_count, &count_type));
  NODE_API_ASSERT(count_type == napi_number);
  NODE_API_CALL(napi_get_value_int32(env, my_count, &count));

  NODE_EMBEDDING_CALL(
      node_embedding_runtime_node_api_scope_close(runtime, node_api_scope));
  if (count == 5) {
    NODE_EMBEDDING_CALL(node_embedding_runtime_event_loop_run(runtime));
    fprintf(stdout, "%d\n", count);
    test_ui_queue_stop(&test_task->test_data->ui_queue);
  }
on_exit:
  if (embedding_status != node_embedding_status_ok) {
    ThrowLastErrorMessage(env,
                          "RunTestTask failed: %s\n",
                          node_embedding_last_error_message_get());
    status = napi_generic_failure;
  }
  GetAndThrowLastErrorMessage(env, status);
}

static void ReleaseTestTask4(void* cb_data) {
  test_ext_task_t* test_task = (test_ext_task_t*)cb_data;
  if (test_task->release_task_data != NULL) {
    test_task->release_task_data(test_task->task_data);
  }
  free(test_task);
}

// The callback will be invoked from the runtime's event loop observer thread.
// It must schedule the work to the UI thread's event loop.
static node_embedding_status PostTask4(
    void* cb_data,
    node_embedding_task_run_callback run_task,
    void* task_data,
    node_embedding_data_release_callback release_task_data,
    bool* succeeded) {
  test_data4_t* test_data = (test_data4_t*)cb_data;
  test_ext_task_t* test_task =
      (test_ext_task_t*)malloc(sizeof(test_ext_task_t));
  if (test_task == NULL) {
    return node_embedding_status_out_of_memory;
  }
  memset(test_task, 0, sizeof(test_ext_task_t));
  test_task_init(
      &test_task->parent_task, test_task, RunTestTask4, ReleaseTestTask4);
  test_task->run_task = run_task;
  test_task->task_data = task_data;
  test_task->release_task_data = release_task_data;
  test_task->test_data = test_data;

  test_ui_queue_post_task(&test_data->ui_queue, &test_task->parent_task);
  if (succeeded != NULL) {
    *succeeded = true;
  }
  return node_embedding_status_ok;
}

static node_embedding_status ConfigureRuntime4(
    void* cb_data,
    node_embedding_platform platform,
    node_embedding_runtime_config runtime_config) {
  node_embedding_status embedding_status = node_embedding_status_ok;
  NODE_EMBEDDING_CALL(node_embedding_runtime_config_set_task_runner(
      runtime_config, PostTask4, cb_data, NULL));
  NODE_EMBEDDING_CALL(LoadUtf8Script(runtime_config, main_script));
on_exit:
  return embedding_status;
}

static void StartProcessing4(void* cb_data) {
  napi_status status = napi_ok;
  node_embedding_status embedding_status = node_embedding_status_ok;
  test_data4_t* data = (test_data4_t*)cb_data;
  node_embedding_node_api_scope node_api_scope;
  napi_env env;
  NODE_EMBEDDING_CALL(node_embedding_runtime_node_api_scope_open(
      data->runtime, &node_api_scope, &env));

  napi_value undefined, global, func;
  NODE_API_CALL(napi_get_undefined(env, &undefined));
  NODE_API_CALL(napi_get_global(env, &global));
  NODE_API_CALL(napi_get_named_property(env, global, "incMyCount", &func));

  napi_valuetype func_type;
  NODE_API_CALL(napi_typeof(env, func, &func_type));
  NODE_API_ASSERT(func_type == napi_function);
  NODE_API_CALL(napi_call_function(env, undefined, func, 0, NULL, NULL));

  NODE_EMBEDDING_CALL(node_embedding_runtime_node_api_scope_close(
      data->runtime, node_api_scope));
on_exit:
  // TODO: extract into a separate function
  if (embedding_status != node_embedding_status_ok) {
    ThrowLastErrorMessage(env,
                          "RunTestTask failed: %s\n",
                          node_embedding_last_error_message_get());
    status = napi_pending_exception;
  }
  GetAndThrowLastErrorMessage(env, status);
}

// Tests that a the runtime's event loop can be called from the UI thread
// event loop.
int32_t test_main_c_api_threading_runtime_in_ui_thread(int32_t argc,
                                                       const char* argv[]) {
  // A simulation of the UI thread's event loop implemented as a dispatcher
  // queue. Note that it is a very simplistic implementation not suitable
  // for the real apps.
  node_embedding_status embedding_status = node_embedding_status_ok;
  test_data4_t test_data = {0};
  test_ui_queue_init(&test_data.ui_queue);

  node_embedding_platform platform;
  NODE_EMBEDDING_CALL(node_embedding_platform_create(
      NODE_EMBEDDING_VERSION, argc, argv, NULL, NULL, &platform));
  if (platform == NULL) {
    goto on_exit;  // early return
  }

  NODE_EMBEDDING_CALL(node_embedding_runtime_create(
      platform, ConfigureRuntime4, &test_data, &test_data.runtime));

  // The initial task starts the JS code that then will do the timer
  // scheduling. The timer supposed to be handled by the runtime's event loop.
  test_task_t task;
  test_task_init(&task, &test_data, StartProcessing4, NULL);
  test_ui_queue_post_task(&test_data.ui_queue, &task);

  test_ui_queue_run(&test_data.ui_queue);
  test_ui_queue_destroy(&test_data.ui_queue);

  NODE_EMBEDDING_CALL(node_embedding_runtime_delete(test_data.runtime));
  NODE_EMBEDDING_CALL(node_embedding_platform_delete(platform));
on_exit:
  return StatusToExitCode(PrintErrorMessage(argv[0], embedding_status));
}
