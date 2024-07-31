// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <cstdio>
#include <exception>
#include <vector>

#include "src/base/logging.h"
#include "tools/v8windbg/test/debug-callbacks.h"

// See the docs at
// https://docs.microsoft.com/en-us/windows-hardware/drivers/debugger/using-the-debugger-engine-api

namespace v8 {
namespace internal {
namespace v8windbg_test {

namespace {

// Loads a named extension library upon construction and unloads it upon
// destruction.
class V8_NODISCARD LoadExtensionScope {
 public:
  LoadExtensionScope(WRL::ComPtr<IDebugControl4> p_debug_control,
                     std::wstring extension_path)
      : p_debug_control_(p_debug_control),
        extension_path_(std::move(extension_path)) {
    p_debug_control->AddExtensionWide(extension_path_.c_str(), 0, &ext_handle_);
    // HACK: Below fails, but is required for the extension to actually
    // initialize. Just the AddExtension call doesn't actually load and
    // initialize it.
    p_debug_control->CallExtension(ext_handle_, "Foo", "Bar");
  }
  ~LoadExtensionScope() {
    // Let the extension uninitialize so it can deallocate memory, meaning any
    // reported memory leaks should be real bugs.
    p_debug_control_->RemoveExtension(ext_handle_);
  }

 private:
  LoadExtensionScope(const LoadExtensionScope&) = delete;
  LoadExtensionScope& operator=(const LoadExtensionScope&) = delete;
  WRL::ComPtr<IDebugControl4> p_debug_control_;
  ULONG64 ext_handle_;
  // This string is part of the heap snapshot when the extension loads, so keep
  // it alive until after the extension unloads and checks for any heap changes.
  std::wstring extension_path_;
};

// Initializes COM upon construction and uninitializes it upon destruction.
class V8_NODISCARD ComScope {
 public:
  ComScope() { hr_ = CoInitializeEx(nullptr, COINIT_MULTITHREADED); }
  ~ComScope() {
    // "To close the COM library gracefully on a thread, each successful call to
    // CoInitialize or CoInitializeEx, including any call that returns S_FALSE,
    // must be balanced by a corresponding call to CoUninitialize."
    // https://docs.microsoft.com/en-us/windows/win32/api/combaseapi/nf-combaseapi-coinitializeex
    if (SUCCEEDED(hr_)) {
      CoUninitialize();
    }
  }
  HRESULT hr() { return hr_; }

 private:
  HRESULT hr_;
};

// Sets a breakpoint. Returns S_OK if the function name resolved successfully
// and the breakpoint is in a non-deferred state.
HRESULT SetBreakpoint(WRL::ComPtr<IDebugControl4> p_debug_control,
                      const char* function_name) {
  WRL::ComPtr<IDebugBreakpoint> bp;
  HRESULT hr =
      p_debug_control->AddBreakpoint(DEBUG_BREAKPOINT_CODE, DEBUG_ANY_ID, &bp);
  if (FAILED(hr)) return hr;
  hr = bp->SetOffsetExpression(function_name);
  if (FAILED(hr)) return hr;
  hr = bp->AddFlags(DEBUG_BREAKPOINT_ENABLED);
  if (FAILED(hr)) return hr;

  // Check whether the symbol could be found.
  uint64_t offset;
  hr = bp->GetOffset(&offset);
  return hr;
}

// Sets a breakpoint. Depending on the build configuration, the function might
// be in the v8 or d8 module, so this function tries to set both.
HRESULT SetBreakpointInV8OrD8(WRL::ComPtr<IDebugControl4> p_debug_control,
                              const std::string& function_name) {
  // Component builds call the V8 module "v8". Try this first, because there is
  // also a module named "d8" or "d8_exe" where we should avoid attempting to
  // set a breakpoint.
  HRESULT hr = SetBreakpoint(p_debug_control, ("v8!" + function_name).c_str());
  if (SUCCEEDED(hr)) return hr;

  // x64 release builds call it "d8".
  hr = SetBreakpoint(p_debug_control, ("d8!" + function_name).c_str());
  if (SUCCEEDED(hr)) return hr;

  // x86 release builds call it "d8_exe".
  return SetBreakpoint(p_debug_control, ("d8_exe!" + function_name).c_str());
}

void RunAndCheckOutput(const char* friendly_name, const char* command,
                       std::vector<const char*> expected_substrings,
                       MyOutput* output, IDebugControl4* p_debug_control) {
  output->ClearLog();
  CHECK(SUCCEEDED(p_debug_control->Execute(DEBUG_OUTCTL_ALL_CLIENTS, command,
                                           DEBUG_EXECUTE_ECHO)));
  for (const char* expected : expected_substrings) {
    CHECK(output->GetLog().find(expected) != std::string::npos);
  }
}

}  // namespace

void RunTests() {
  // Initialize COM... Though it doesn't seem to matter if you don't!
  ComScope com_scope;
  CHECK(SUCCEEDED(com_scope.hr()));

  // Get the file path of the module containing this test function. It should be
  // in the output directory alongside the data dependencies required by this
  // test (d8.exe, v8windbg.dll, and v8windbg-test-script.js).
  HMODULE module = nullptr;
  bool success =
      GetModuleHandleEx(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                            GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                        reinterpret_cast<LPCWSTR>(&RunTests), &module);
  CHECK(success);
  wchar_t this_module_path[MAX_PATH];
  DWORD path_size = GetModuleFileName(module, this_module_path, MAX_PATH);
  CHECK(path_size != 0);
  HRESULT hr = PathCchRemoveFileSpec(this_module_path, MAX_PATH);
  CHECK(SUCCEEDED(hr));

  // Get the Debug client
  WRL::ComPtr<IDebugClient5> p_client;
  hr = DebugCreate(__uuidof(IDebugClient5), &p_client);
  CHECK(SUCCEEDED(hr));

  WRL::ComPtr<IDebugSymbols3> p_symbols;
  hr = p_client->QueryInterface(__uuidof(IDebugSymbols3), &p_symbols);
  CHECK(SUCCEEDED(hr));

  // Symbol loading fails if the pdb is in the same folder as the exe, but it's
  // not on the path.
  hr = p_symbols->SetSymbolPathWide(this_module_path);
  CHECK(SUCCEEDED(hr));

  // Set the event callbacks
  MyCallback callback;
  hr = p_client->SetEventCallbacks(&callback);
  CHECK(SUCCEEDED(hr));

  // Launch the process with the debugger attached
  std::wstring command_line =
      std::wstring(L"\"") + this_module_path + L"\\d8.exe\" \"" +
      this_module_path + L"\\obj\\tools\\v8windbg\\v8windbg-test-script.js\"";
  DEBUG_CREATE_PROCESS_OPTIONS proc_options;
  proc_options.CreateFlags = DEBUG_PROCESS;
  proc_options.EngCreateFlags = 0;
  proc_options.VerifierFlags = 0;
  proc_options.Reserved = 0;
  hr = p_client->CreateProcessWide(
      0, const_cast<wchar_t*>(command_line.c_str()), DEBUG_PROCESS);
  CHECK(SUCCEEDED(hr));

  // Wait for the attach event
  WRL::ComPtr<IDebugControl4> p_debug_control;
  hr = p_client->QueryInterface(__uuidof(IDebugControl4), &p_debug_control);
  CHECK(SUCCEEDED(hr));
  hr = p_debug_control->WaitForEvent(0, INFINITE);
  CHECK(SUCCEEDED(hr));

  // Break again after non-delay-load modules are loaded.
  hr = p_debug_control->AddEngineOptions(DEBUG_ENGOPT_INITIAL_BREAK);
  CHECK(SUCCEEDED(hr));
  hr = p_debug_control->WaitForEvent(0, INFINITE);
  CHECK(SUCCEEDED(hr));

  // Set a breakpoint in a C++ function called by the script.
  hr = SetBreakpointInV8OrD8(p_debug_control, "v8::internal::JsonStringify");
  CHECK(SUCCEEDED(hr));

  hr = p_debug_control->SetExecutionStatus(DEBUG_STATUS_GO);
  CHECK(SUCCEEDED(hr));

  // Wait for the breakpoint.
  hr = p_debug_control->WaitForEvent(0, INFINITE);
  CHECK(SUCCEEDED(hr));

  ULONG type, proc_id, thread_id, desc_used;
  uint8_t desc[1024];
  hr = p_debug_control->GetLastEventInformation(
      &type, &proc_id, &thread_id, nullptr, 0, nullptr,
      reinterpret_cast<PSTR>(desc), 1024, &desc_used);
  CHECK(SUCCEEDED(hr));

  LoadExtensionScope extension_loaded(
      p_debug_control, this_module_path + std::wstring(L"\\v8windbg.dll"));

  // Set the output callbacks after the extension is loaded, so it gets
  // destroyed before the extension unloads. This avoids reporting incorrectly
  // reporting that the output buffer was leaked during extension teardown.
  MyOutput output(p_client);

  // Set stepping mode.
  hr = p_debug_control->SetCodeLevel(DEBUG_LEVEL_SOURCE);
  CHECK(SUCCEEDED(hr));

  // Do some actual testing
  RunAndCheckOutput("bitfields",
                    "p;dx replacer.Value.shared_function_info.flags",
                    {"kNamedExpression"}, &output, p_debug_control.Get());

  RunAndCheckOutput("in-object properties",
                    "dx object.Value.@\"in-object properties\"[1]",
                    {"NullValue", "Oddball"}, &output, p_debug_control.Get());

  RunAndCheckOutput(
      "arrays of structs",
      "dx object.Value.map.instance_descriptors.descriptors[1].key",
      {"\"secondProp\"", "SeqOneByteString"}, &output, p_debug_control.Get());

  // TODO(v8:11527): enable this when symbol information for the in-Isolate
  // builtins is available.
  // RunAndCheckOutput(
  //     "local variables",
  //     "dx -r1 @$curthread.Stack.Frames.Where(f => "
  //     "f.ToDisplayString().Contains(\"InterpreterEntryTrampoline\")).Skip(1)."
  //     "First().LocalVariables.@\"memory interpreted as Objects\"",
  //     {"\"hello\""}, &output, p_debug_control.Get());

  RunAndCheckOutput("js stack", "dx @$jsstack()[0].function_name",
                    {"\"a\"", "SeqOneByteString"}, &output,
                    p_debug_control.Get());

  RunAndCheckOutput("js stack", "dx @$jsstack()[1].function_name",
                    {"\"b\"", "SeqOneByteString"}, &output,
                    p_debug_control.Get());

  RunAndCheckOutput("js stack", "dx @$jsstack()[2].function_name",
                    {"empty_string \"\"", "SeqOneByteString"}, &output,
                    p_debug_control.Get());

  // Test for @$curisolate(). This should have the same output with
  // `dx v8::internal::g_current_isolate_`.
  output.ClearLog();
  CHECK(SUCCEEDED(p_debug_control->Execute(
      DEBUG_OUTCTL_ALL_CLIENTS, "dx v8::internal::g_current_isolate_",
      DEBUG_EXECUTE_ECHO)));
  size_t addr_pos = output.GetLog().find("0x");
  CHECK(addr_pos != std::string::npos);
  std::string expected_output = output.GetLog().substr(addr_pos);

  output.ClearLog();
  CHECK(SUCCEEDED(p_debug_control->Execute(
      DEBUG_OUTCTL_ALL_CLIENTS, "dx @$curisolate()", DEBUG_EXECUTE_ECHO)));
  CHECK_EQ(output.GetLog().substr(output.GetLog().find("0x")), expected_output);

  // Detach before exiting
  hr = p_client->DetachProcesses();
  CHECK(SUCCEEDED(hr));
}

}  // namespace v8windbg_test
}  // namespace internal
}  // namespace v8
