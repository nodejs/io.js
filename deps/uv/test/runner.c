/* Copyright Joyent, Inc. and other Node contributors. All rights reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to
 * deal in the Software without restriction, including without limitation the
 * rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
 * sell copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "runner.h"
#include "task.h"
#include "uv.h"

/* Refs: https://github.com/libuv/libuv/issues/4369 */
#if defined(__ANDROID__)
#include <android/fdsan.h>
#endif

char executable_path[sizeof(executable_path)];


static int compare_task(const void* va, const void* vb) {
  const task_entry_t* a = va;
  const task_entry_t* b = vb;
  return strcmp(a->task_name, b->task_name);
}


char* fmt(char (*buf)[32], double d) {
  uint64_t v;
  char* p;

  p = &(*buf)[32];
  v = (uint64_t) d;

  *--p = '\0';

  if (v == 0)
    *--p = '0';

  while (v) {
    if (v) *--p = '0' + (v % 10), v /= 10;
    if (v) *--p = '0' + (v % 10), v /= 10;
    if (v) *--p = '0' + (v % 10), v /= 10;
    if (v) *--p = ',';
  }

  return p;
}


int run_tests(int benchmark_output) {
  int actual;
  int total;
  int failed;
  int current;
  int test_result;
  int skip;
  task_entry_t* task;

  /* Count the number of tests. */
  actual = 0;
  total = 0;
  for (task = TASKS; task->main; task++, actual++) {
    if (!task->is_helper) {
      total++;
    }
  }

  /* Keep platform_output first. */
  skip = (actual > 0 && 0 == strcmp(TASKS[0].task_name, "platform_output"));
  qsort(TASKS + skip, actual - skip, sizeof(TASKS[0]), compare_task);

  fprintf(stdout, "1..%d\n", total);
  fflush(stdout);

  /* Run all tests. */
  failed = 0;
  current = 1;
  for (task = TASKS; task->main; task++) {
    if (task->is_helper) {
      continue;
    }

    test_result = run_test(task->task_name, benchmark_output, current);
    switch (test_result) {
    case TEST_OK: break;
    case TEST_SKIP: break;
    default: failed++;
    }
    current++;
  }

  return failed;
}


void log_tap_result(int test_count,
                    const char* test,
                    int status,
                    process_info_t* process) {
  const char* result;
  const char* directive;
  char reason[1024];
  int reason_length;

  switch (status) {
  case TEST_OK:
    result = "ok";
    directive = "";
    break;
  case TEST_SKIP:
    result = "ok";
    directive = " # SKIP ";
    break;
  default:
    result = "not ok";
    directive = "";
  }

  if (status == TEST_SKIP && process_output_size(process) > 0) {
    process_read_last_line(process, reason, sizeof reason);
    reason_length = strlen(reason);
    if (reason_length > 0 && reason[reason_length - 1] == '\n')
      reason[reason_length - 1] = '\0';
  } else {
    reason[0] = '\0';
  }

  fprintf(stdout, "%s %d - %s%s%s\n", result, test_count, test, directive, reason);
  fflush(stdout);
}

void enable_fdsan(void) {
/* Refs: https://github.com/libuv/libuv/issues/4369 */
#if defined(__ANDROID__)
  android_fdsan_set_error_level(ANDROID_FDSAN_ERROR_LEVEL_WARN_ALWAYS);
#endif
}


int run_test(const char* test,
             int benchmark_output,
             int test_count) {
  char errmsg[1024] = "";
  process_info_t processes[1024];
  process_info_t *main_proc;
  task_entry_t* task;
  int timeout_multiplier;
  int process_count;
  int result;
  int status;
  int i;

  status = 255;
  main_proc = NULL;
  process_count = 0;

  enable_fdsan();

#ifndef _WIN32
  /* Clean up stale socket from previous run. */
  remove(TEST_PIPENAME);
  remove(TEST_PIPENAME_2);
  remove(TEST_PIPENAME_3);
#endif

  /* If it's a helper the user asks for, start it directly. */
  for (task = TASKS; task->main; task++) {
    if (task->is_helper && strcmp(test, task->process_name) == 0) {
      return task->main();
    }
  }

  /* Start the helpers first. */
  for (task = TASKS; task->main; task++) {
    if (strcmp(test, task->task_name) != 0) {
      continue;
    }

    /* Skip the test itself. */
    if (!task->is_helper) {
      continue;
    }

    if (process_start(task->task_name,
                      task->process_name,
                      &processes[process_count],
                      1 /* is_helper */) == -1) {
      snprintf(errmsg,
               sizeof errmsg,
               "Process `%s` failed to start.",
               task->process_name);
      goto out;
    }

    process_count++;
  }

  /* Now start the test itself. */
  for (task = TASKS; task->main; task++) {
    if (strcmp(test, task->task_name) != 0) {
      continue;
    }

    if (task->is_helper) {
      continue;
    }

    if (process_start(task->task_name,
                      task->process_name,
                      &processes[process_count],
                      0 /* !is_helper */) == -1) {
      snprintf(errmsg,
               sizeof errmsg,
               "Process `%s` failed to start.",
               task->process_name);
      goto out;
    }

    main_proc = &processes[process_count];
    process_count++;
    break;
  }

  if (main_proc == NULL) {
    snprintf(errmsg,
             sizeof errmsg,
             "No test with that name: %s",
             test);
    goto out;
  }

  timeout_multiplier = 1;
#ifndef _WIN32
  do {
    const char* var;

    var = getenv("UV_TEST_TIMEOUT_MULTIPLIER");
    if (var == NULL)
      break;

    timeout_multiplier = atoi(var);
    if (timeout_multiplier <= 0)
      timeout_multiplier = 1;
  } while (0);
#endif

  result = process_wait(main_proc, 1, task->timeout * timeout_multiplier);
  if (result == -1) {
    FATAL("process_wait failed");
  } else if (result == -2) {
    /* Don't have to clean up the process, process_wait() has killed it. */
    snprintf(errmsg,
             sizeof errmsg,
             "timeout");
    goto out;
  }

  status = process_reap(main_proc);
  if (status != TEST_OK) {
    snprintf(errmsg,
             sizeof errmsg,
             "exit code %d",
             status);
    goto out;
  }

  if (benchmark_output) {
    /* Give the helpers time to clean up their act. */
    uv_sleep(1000);
  }

out:
  /* Reap running processes except the main process, it's already dead. */
  for (i = 0; i < process_count - 1; i++) {
    process_terminate(&processes[i]);
  }

  if (process_count > 0 &&
      process_wait(processes, process_count - 1, -1) < 0) {
    FATAL("process_wait failed");
  }

  log_tap_result(test_count, test, status, &processes[i]);

  /* Show error and output from processes if the test failed. */
  if ((status != TEST_OK && status != TEST_SKIP) || task->show_output) {
    if (strlen(errmsg) > 0)
      fprintf(stdout, "# %s\n", errmsg);
    fprintf(stdout, "# ");
    fflush(stdout);

    for (i = 0; i < process_count; i++) {
      switch (process_output_size(&processes[i])) {
       case -1:
        fprintf(stdout, "Output from process `%s`: (unavailable)\n",
                process_get_name(&processes[i]));
        fflush(stdout);
        break;

       case 0:
        fprintf(stdout, "Output from process `%s`: (no output)\n",
                process_get_name(&processes[i]));
        fflush(stdout);
        break;

       default:
        fprintf(stdout, "Output from process `%s`:\n", process_get_name(&processes[i]));
        fflush(stdout);
        process_copy_output(&processes[i], stdout);
        break;
      }
    }

  /* In benchmark mode show concise output from the main process. */
  } else if (benchmark_output) {
    switch (process_output_size(main_proc)) {
     case -1:
      fprintf(stdout, "%s: (unavailable)\n", test);
      fflush(stdout);
      break;

     case 0:
      fprintf(stdout, "%s: (no output)\n", test);
      fflush(stdout);
      break;

     default:
      for (i = 0; i < process_count; i++) {
        process_copy_output(&processes[i], stdout);
      }
      break;
    }
  }

  /* Clean up all process handles. */
  for (i = 0; i < process_count; i++) {
    process_cleanup(&processes[i]);
  }

  return status;
}


/* Returns the status code of the task part
 * or 255 if no matching task was not found.
 */
int run_test_part(const char* test, const char* part) {
  task_entry_t* task;
  int r;

  for (task = TASKS; task->main; task++) {
    if (strcmp(test, task->task_name) == 0 &&
        strcmp(part, task->process_name) == 0) {
      r = task->main();
      return r;
    }
  }

  fprintf(stdout, "No test part with that name: %s:%s\n", test, part);
  fflush(stdout);
  return 255;
}



static int find_helpers(const task_entry_t* task,
                        const task_entry_t** helpers) {
  const task_entry_t* helper;
  int n_helpers;

  for (n_helpers = 0, helper = TASKS; helper->main; helper++) {
    if (helper->is_helper && strcmp(helper->task_name, task->task_name) == 0) {
      *helpers++ = helper;
      n_helpers++;
    }
  }

  return n_helpers;
}


void print_tests(FILE* stream) {
  const task_entry_t* helpers[1024];
  const task_entry_t* task;
  int n_helpers;
  int n_tasks;
  int i;

  for (n_tasks = 0, task = TASKS; task->main; n_tasks++, task++);
  qsort(TASKS, n_tasks, sizeof(TASKS[0]), compare_task);

  for (task = TASKS; task->main; task++) {
    if (task->is_helper) {
      continue;
    }

    n_helpers = find_helpers(task, helpers);
    if (n_helpers) {
      printf("%-25s (helpers:", task->task_name);
      for (i = 0; i < n_helpers; i++) {
        printf(" %s", helpers[i]->process_name);
      }
      printf(")\n");
    } else {
      printf("%s\n", task->task_name);
    }
  }
}


void print_lines(const char* buffer, size_t size, FILE* stream) {
  const char* start;
  const char* end;

  start = buffer;
  while ((end = memchr(start, '\n', &buffer[size] - start))) {
    fputs("# ", stream);
    fwrite(start, 1, (int)(end - start), stream);
    fputs("\n", stream);
    fflush(stream);
    start = end + 1;
  }

  end = &buffer[size];
  if (start < end) {
    fputs("# ", stream);
    fwrite(start, 1, (int)(end - start), stream);
    fputs("\n", stream);
    fflush(stream);
  }
}
