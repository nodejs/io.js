// Copyright 2012 the V8 project authors. All rights reserved.
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are
// met:
//
//     * Redistributions of source code must retain the above copyright
//       notice, this list of conditions and the following disclaimer.
//     * Redistributions in binary form must reproduce the above
//       copyright notice, this list of conditions and the following
//       disclaimer in the documentation and/or other materials provided
//       with the distribution.
//     * Neither the name of Google Inc. nor the names of its
//       contributors may be used to endorse or promote products derived
//       from this software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
// "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
// LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
// A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
// OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
// SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
// LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
// DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
// THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
// (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
// OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

#include <assert.h>
#include <fcntl.h>
#include <include/libplatform/libplatform.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "include/v8-context.h"
#include "include/v8-exception.h"
#include "include/v8-initialization.h"
#include "include/v8-isolate.h"
#include "include/v8-local-handle.h"
#include "include/v8-script.h"
#include "include/v8-template.h"

/**
 * This sample program shows how to implement a simple javascript shell
 * based on V8.  This includes initializing V8 with command line options,
 * creating global functions, compiling and executing strings.
 *
 * For a more sophisticated shell, consider using the debug shell D8.
 */

v8::Global<v8::Context> CreateShellContext(v8::Isolate* isolate);
void RunShell(v8::Isolate* isolate, const v8::Global<v8::Context>& context,
              v8::Platform* platform);
int RunMain(v8::Isolate* isolate, const v8::Global<v8::Context>& context,
            v8::Platform* platform, int argc, char* argv[]);
bool ExecuteString(v8::Isolate* isolate, v8::Local<v8::String> source,
                   v8::Local<v8::Value> name, bool print_result,
                   bool report_exceptions);
void Print(const v8::FunctionCallbackInfo<v8::Value>& info);
void Read(const v8::FunctionCallbackInfo<v8::Value>& info);
void Load(const v8::FunctionCallbackInfo<v8::Value>& info);
void Quit(const v8::FunctionCallbackInfo<v8::Value>& info);
void Version(const v8::FunctionCallbackInfo<v8::Value>& info);
v8::MaybeLocal<v8::String> ReadFile(v8::Isolate* isolate, const char* name);
void ReportException(v8::Isolate* isolate, v8::TryCatch* handler);

static bool run_shell;

int main(int argc, char* argv[]) {
  v8::V8::InitializeICUDefaultLocation(argv[0]);
  v8::V8::InitializeExternalStartupData(argv[0]);
  std::unique_ptr<v8::Platform> platform = v8::platform::NewDefaultPlatform();
  v8::V8::InitializePlatform(platform.get());
  v8::V8::SetFlagsFromCommandLine(&argc, argv, true);
  v8::V8::Initialize();
  v8::Isolate::CreateParams create_params;
  create_params.array_buffer_allocator =
      v8::ArrayBuffer::Allocator::NewDefaultAllocator();
  v8::Isolate* isolate = v8::Isolate::New(create_params);
  run_shell = (argc == 1);
  int result;
  {
    v8::Isolate::Scope isolate_scope(isolate);
    v8::Global<v8::Context> context = CreateShellContext(isolate);
    if (context.IsEmpty()) {
      fprintf(stderr, "Error creating context\n");
      return 1;
    }
    result = RunMain(isolate, context, platform.get(), argc, argv);
    if (run_shell) RunShell(isolate, context, platform.get());
  }
  isolate->Dispose();
  v8::V8::Dispose();
  v8::V8::DisposePlatform();
  delete create_params.array_buffer_allocator;
  return result;
}

// Extracts a C string from a V8 Utf8Value.
const char* ToCString(const v8::String::Utf8Value& value) {
  return *value ? *value : "<string conversion failed>";
}

// Creates a new execution environment containing the built-in
// functions.
v8::Global<v8::Context> CreateShellContext(v8::Isolate* isolate) {
  v8::HandleScope handle_scope(isolate);
  // Create a template for the global object.
  v8::Local<v8::ObjectTemplate> global = v8::ObjectTemplate::New(isolate);
  // Bind the global 'print' function to the C++ Print callback.
  global->Set(isolate, "print", v8::FunctionTemplate::New(isolate, Print));
  // Bind the global 'read' function to the C++ Read callback.
  global->Set(isolate, "read", v8::FunctionTemplate::New(isolate, Read));
  // Bind the global 'load' function to the C++ Load callback.
  global->Set(isolate, "load", v8::FunctionTemplate::New(isolate, Load));
  // Bind the 'quit' function
  global->Set(isolate, "quit", v8::FunctionTemplate::New(isolate, Quit));
  // Bind the 'version' function
  global->Set(isolate, "version", v8::FunctionTemplate::New(isolate, Version));
  // Return the context.
  v8::Local<v8::Context> context = v8::Context::New(isolate, nullptr, global);
  return v8::Global<v8::Context>(isolate, context);
}

// The callback that is invoked by v8 whenever the JavaScript 'print'
// function is called.  Prints its arguments on stdout separated by
// spaces and ending with a newline.
void Print(const v8::FunctionCallbackInfo<v8::Value>& info) {
  bool first = true;
  for (int i = 0; i < info.Length(); i++) {
    v8::HandleScope handle_scope(info.GetIsolate());
    if (first) {
      first = false;
    } else {
      printf(" ");
    }
    v8::String::Utf8Value str(info.GetIsolate(), info[i]);
    const char* cstr = ToCString(str);
    printf("%s", cstr);
  }
  printf("\n");
  fflush(stdout);
}

// The callback that is invoked by v8 whenever the JavaScript 'read'
// function is called.  This function loads the content of the file named in
// the argument into a JavaScript string.
void Read(const v8::FunctionCallbackInfo<v8::Value>& info) {
  if (info.Length() != 1) {
    info.GetIsolate()->ThrowError("Bad parameters");
    return;
  }
  v8::String::Utf8Value file(info.GetIsolate(), info[0]);
  if (*file == nullptr) {
    info.GetIsolate()->ThrowError("Error loading file");
    return;
  }
  v8::Local<v8::String> source;
  if (!ReadFile(info.GetIsolate(), *file).ToLocal(&source)) {
    info.GetIsolate()->ThrowError("Error loading file");
    return;
  }

  info.GetReturnValue().Set(source);
}

// The callback that is invoked by v8 whenever the JavaScript 'load'
// function is called.  Loads, compiles and executes its argument
// JavaScript file.
void Load(const v8::FunctionCallbackInfo<v8::Value>& info) {
  for (int i = 0; i < info.Length(); i++) {
    v8::HandleScope handle_scope(info.GetIsolate());
    v8::String::Utf8Value file(info.GetIsolate(), info[i]);
    if (*file == nullptr) {
      info.GetIsolate()->ThrowError("Error loading file");
      return;
    }
    v8::Local<v8::String> source;
    if (!ReadFile(info.GetIsolate(), *file).ToLocal(&source)) {
      info.GetIsolate()->ThrowError("Error loading file");
      return;
    }
    if (!ExecuteString(info.GetIsolate(), source, info[i], false, false)) {
      info.GetIsolate()->ThrowError("Error executing file");
      return;
    }
  }
}

// The callback that is invoked by v8 whenever the JavaScript 'quit'
// function is called.  Quits.
void Quit(const v8::FunctionCallbackInfo<v8::Value>& info) {
  // If not arguments are given info[0] will yield undefined which
  // converts to the integer value 0.
  int exit_code =
      info[0]->Int32Value(info.GetIsolate()->GetCurrentContext()).FromMaybe(0);
  fflush(stdout);
  fflush(stderr);
  exit(exit_code);
}

// The callback that is invoked by v8 whenever the JavaScript 'version'
// function is called.  Returns a string containing the current V8 version.
void Version(const v8::FunctionCallbackInfo<v8::Value>& info) {
  info.GetReturnValue().Set(
      v8::String::NewFromUtf8(info.GetIsolate(), v8::V8::GetVersion())
          .ToLocalChecked());
}

// Reads a file into a v8 string.
v8::MaybeLocal<v8::String> ReadFile(v8::Isolate* isolate, const char* name) {
  FILE* file = fopen(name, "rb");
  if (file == nullptr) return {};

  fseek(file, 0, SEEK_END);
  size_t size = ftell(file);
  rewind(file);

  char* chars = new char[size + 1];
  chars[size] = '\0';
  for (size_t i = 0; i < size;) {
    i += fread(&chars[i], 1, size - i, file);
    if (ferror(file)) {
      fclose(file);
      return {};
    }
  }
  fclose(file);
  v8::MaybeLocal<v8::String> result = v8::String::NewFromUtf8(
      isolate, chars, v8::NewStringType::kNormal, static_cast<int>(size));
  delete[] chars;
  return result;
}

// Process remaining command line arguments and execute files
int RunMain(v8::Isolate* isolate, const v8::Global<v8::Context>& context,
            v8::Platform* platform, int argc, char* argv[]) {
  for (int i = 1; i < argc; i++) {
    const char* str = argv[i];
    if (strcmp(str, "--shell") == 0) {
      run_shell = true;
    } else if (strcmp(str, "-f") == 0) {
      // Ignore any -f flags for compatibility with the other stand-
      // alone JavaScript engines.
      continue;
    } else if (strncmp(str, "--", 2) == 0) {
      fprintf(stderr,
              "Warning: unknown flag %s.\nTry --help for options\n", str);
    } else if (strcmp(str, "-e") == 0 && i + 1 < argc) {
      // Execute argument given to -e option directly.
      bool success;
      {
        // Enter the execution environment before evaluating any code.
        v8::HandleScope handle_scope(isolate);
        v8::Context::Scope context_scope(context.Get(isolate));
        v8::Local<v8::String> file_name =
            v8::String::NewFromUtf8Literal(isolate, "unnamed");
        v8::Local<v8::String> source;
        if (!v8::String::NewFromUtf8(isolate, argv[++i]).ToLocal(&source)) {
          return 1;
        }
        success = ExecuteString(isolate, source, file_name, false, true);
      }
      // It is important not to pump the message loop when there are v8::Local
      // handles on the stack, as this may trigger a stackless GC.
      while (v8::platform::PumpMessageLoop(platform, isolate)) continue;
      if (!success) return 1;
    } else {
      // Use all other arguments as names of files to load and run.
      bool success;
      {
        // Enter the execution environment before evaluating any code.
        v8::HandleScope handle_scope(isolate);
        v8::Context::Scope context_scope(context.Get(isolate));
        v8::Local<v8::String> file_name =
            v8::String::NewFromUtf8(isolate, str).ToLocalChecked();
        v8::Local<v8::String> source;
        if (!ReadFile(isolate, str).ToLocal(&source)) {
          fprintf(stderr, "Error reading '%s'\n", str);
          continue;
        }
        success = ExecuteString(isolate, source, file_name, false, true);
      }
      // It is important not to pump the message loop when there are v8::Local
      // handles on the stack, as this may trigger a stackless GC.
      while (v8::platform::PumpMessageLoop(platform, isolate)) continue;
      if (!success) return 1;
    }
  }
  return 0;
}

// The read-eval-execute loop of the shell.
void RunShell(v8::Isolate* isolate, const v8::Global<v8::Context>& context,
              v8::Platform* platform) {
  fprintf(stderr, "V8 version %s [sample shell]\n", v8::V8::GetVersion());
  static const int kBufferSize = 256;
  while (true) {
    char buffer[kBufferSize];
    fprintf(stderr, "> ");
    char* str = fgets(buffer, kBufferSize, stdin);
    if (str == nullptr) break;
    {
      // Enter the execution environment before evaluating any code.
      v8::HandleScope handle_scope(isolate);
      v8::Context::Scope context_scope(context.Get(isolate));
      v8::Local<v8::String> name(
          v8::String::NewFromUtf8Literal(isolate, "(shell)"));
      ExecuteString(isolate,
                    v8::String::NewFromUtf8(isolate, str).ToLocalChecked(),
                    name, true, true);
    }
    // It is important not to pump the message loop when there are v8::Local
    // handles on the stack, as this may trigger a stackless GC.
    while (v8::platform::PumpMessageLoop(platform, isolate)) continue;
  }
  fprintf(stderr, "\n");
}

// Executes a string within the current v8 context.
bool ExecuteString(v8::Isolate* isolate, v8::Local<v8::String> source,
                   v8::Local<v8::Value> name, bool print_result,
                   bool report_exceptions) {
  v8::HandleScope handle_scope(isolate);
  v8::TryCatch try_catch(isolate);
  v8::ScriptOrigin origin(name);
  v8::Local<v8::Context> context(isolate->GetCurrentContext());
  v8::Local<v8::Script> script;
  if (!v8::Script::Compile(context, source, &origin).ToLocal(&script)) {
    // Print errors that happened during compilation.
    if (report_exceptions)
      ReportException(isolate, &try_catch);
    return false;
  } else {
    v8::Local<v8::Value> result;
    if (!script->Run(context).ToLocal(&result)) {
      assert(try_catch.HasCaught());
      // Print errors that happened during execution.
      if (report_exceptions)
        ReportException(isolate, &try_catch);
      return false;
    } else {
      assert(!try_catch.HasCaught());
      if (print_result && !result->IsUndefined()) {
        // If all went well and the result wasn't undefined then print
        // the returned value.
        v8::String::Utf8Value str(isolate, result);
        const char* cstr = ToCString(str);
        printf("%s\n", cstr);
      }
      return true;
    }
  }
}

void ReportException(v8::Isolate* isolate, v8::TryCatch* try_catch) {
  v8::HandleScope handle_scope(isolate);
  v8::String::Utf8Value exception(isolate, try_catch->Exception());
  const char* exception_string = ToCString(exception);
  v8::Local<v8::Message> message = try_catch->Message();
  if (message.IsEmpty()) {
    // V8 didn't provide any extra information about this error; just
    // print the exception.
    fprintf(stderr, "%s\n", exception_string);
  } else {
    // Print (filename):(line number): (message).
    v8::String::Utf8Value filename(isolate,
                                   message->GetScriptOrigin().ResourceName());
    v8::Local<v8::Context> context(isolate->GetCurrentContext());
    const char* filename_string = ToCString(filename);
    int linenum = message->GetLineNumber(context).FromJust();
    fprintf(stderr, "%s:%i: %s\n", filename_string, linenum, exception_string);
    // Print line of source code.
    v8::String::Utf8Value sourceline(
        isolate, message->GetSourceLine(context).ToLocalChecked());
    const char* sourceline_string = ToCString(sourceline);
    fprintf(stderr, "%s\n", sourceline_string);
    // Print wavy underline (GetUnderline is deprecated).
    int start = message->GetStartColumn(context).FromJust();
    for (int i = 0; i < start; i++) {
      fprintf(stderr, " ");
    }
    int end = message->GetEndColumn(context).FromJust();
    for (int i = start; i < end; i++) {
      fprintf(stderr, "^");
    }
    fprintf(stderr, "\n");
    v8::Local<v8::Value> stack_trace_string;
    if (try_catch->StackTrace(context).ToLocal(&stack_trace_string) &&
        stack_trace_string->IsString() &&
        stack_trace_string.As<v8::String>()->Length() > 0) {
      v8::String::Utf8Value stack_trace(isolate, stack_trace_string);
      const char* err = ToCString(stack_trace);
      fprintf(stderr, "%s\n", err);
    }
  }
}
