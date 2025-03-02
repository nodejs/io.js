// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <windows.h>
#undef INITGUID
#define INITGUID
#include <guiddef.h>

#include <memory>
#include <thread>  // NOLINT(build/c++11)

#include "src/diagnostics/etw-isolate-load-script-data-win.h"
#include "src/diagnostics/etw-isolate-operations-win.h"
#include "src/diagnostics/etw-jit-metadata-win.h"
#include "src/diagnostics/etw-jit-win.h"
#include "src/flags/flags.h"
#include "src/libplatform/etw/etw-provider-win.h"
#include "test/unittests/test-utils.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace v8 {
namespace internal {
namespace ETWJITInterface {

using EtwControlTest = TestWithContext;

DEFINE_GUID(v8_etw_guid, 0x57277741, 0x3638, 0x4A4B, 0xBD, 0xBA, 0x0A, 0xC6,
            0xE4, 0x5D, 0xA5, 0x6C);

void WINAPI ETWEnableCallback(LPCGUID /* source_id */, ULONG is_enabled,
                              UCHAR level, ULONGLONG match_any_keyword,
                              ULONGLONG match_all_keyword,
                              PEVENT_FILTER_DESCRIPTOR filter_data,
                              PVOID /* callback_context */);

class EtwIsolateOperationsMock : public EtwIsolateOperations {
 public:
  MOCK_METHOD(void, SetEtwCodeEventHandler, (Isolate*, uint32_t), (override));
  MOCK_METHOD(void, ResetEtwCodeEventHandler, (Isolate*), (override));

  MOCK_METHOD(FilterETWSessionByURLResult, RunFilterETWSessionByURLCallback,
              (Isolate*, const std::string&), (override));
  MOCK_METHOD(void, RequestInterrupt, (Isolate*, InterruptCallback, void*),
              (override));
  MOCK_METHOD(bool, HeapReadOnlySpaceWritable, (Isolate*), (override));
  MOCK_METHOD(std::optional<Tagged<GcSafeCode>>,
              HeapGcSafeTryFindCodeForInnerPointer, (Isolate*, Address),
              (override));
};

TEST_F(EtwControlTest, Enable) {
  v8_flags.enable_etw_stack_walking = true;

  // Set the flag below for helpful debug spew
  // v8_flags.etw_trace_debug = true;

  testing::NiceMock<EtwIsolateOperationsMock> etw_isolate_operations_mock;
  EtwIsolateOperations::SetInstanceForTesting(&etw_isolate_operations_mock);

  Isolate* isolate = reinterpret_cast<Isolate*>(this->isolate());
  IsolateLoadScriptData::AddIsolate(isolate);

  std::thread isolate_thread;
  EXPECT_CALL(etw_isolate_operations_mock,
              SetEtwCodeEventHandler(testing::Eq(isolate),
                                     testing::Eq(kJitCodeEventDefault)))
      .Times(2);
  EXPECT_CALL(etw_isolate_operations_mock,
              SetEtwCodeEventHandler(testing::Eq(isolate),
                                     testing::Eq(kJitCodeEventEnumExisting |
                                                 ETWJITInterface::kEtwRundown)))
      .Times(1);
  EXPECT_CALL(etw_isolate_operations_mock,
              ResetEtwCodeEventHandler(testing::Eq(isolate)))
      .Times(1);
  ON_CALL(etw_isolate_operations_mock,
          RequestInterrupt(testing::Eq(isolate), testing::_, testing::_))
      .WillByDefault(testing::Invoke(
          [&isolate_thread](Isolate* isolate, InterruptCallback callback,
                            void* data) {
            isolate_thread = std::thread([isolate, callback, data]() {
              callback(reinterpret_cast<v8::Isolate*>(isolate), data);
            });
          }));

  ETWEnableCallback(&v8_etw_guid, kEtwControlEnable, /*level*/ 5,
                    /*match_any_keyword*/ ~0, /*match_all_keyword*/ 0,
                    /*filter_data*/ nullptr, /*callback_context*/ nullptr);
  ASSERT_TRUE(has_active_etw_tracing_session_or_custom_filter);
  isolate_thread.join();

  ETWEnableCallback(&v8_etw_guid, kEtwControlCaptureState, /*level*/ 5,
                    /*match_any_keyword*/ ~0, /*match_all_keyword*/ 0,
                    /*filter_data*/ nullptr, /*callback_context*/ nullptr);
  isolate_thread.join();

  ETWEnableCallback(&v8_etw_guid, kEtwControlEnable, /*level*/ 5,
                    /*match_any_keyword*/ ~0, /*match_all_keyword*/ 0,
                    /*filter_data*/ nullptr, /*callback_context*/ nullptr);
  isolate_thread.join();

  ETWEnableCallback(&v8_etw_guid, kEtwControlDisable, /*level*/ 5,
                    /*match_any_keyword*/ ~0, /*match_all_keyword*/ 0,
                    /*filter_data*/ nullptr, /*callback_context*/ nullptr);
  ASSERT_FALSE(has_active_etw_tracing_session_or_custom_filter);
  isolate_thread.join();

  EtwIsolateOperations::SetInstanceForTesting(nullptr);
}

TEST_F(EtwControlTest, EnableWithFilterData) {
  v8_flags.enable_etw_stack_walking = true;

  // Set the flag below for helpful debug spew
  // v8_flags.etw_trace_debug = true;

  testing::NiceMock<EtwIsolateOperationsMock> etw_isolate_operations_mock;
  EtwIsolateOperations::SetInstanceForTesting(&etw_isolate_operations_mock);

  Isolate* isolate = reinterpret_cast<Isolate*>(this->isolate());
  IsolateLoadScriptData::AddIsolate(isolate);

  std::thread isolate_thread;
  constexpr char origin_filter[] =
      "{\"version\": 2.0, \"description\": \"\", \"filtered_urls\": "
      "[\"https://.*example.com\"], \"trace_interpreter_frames\": true}";
  EXPECT_CALL(etw_isolate_operations_mock,
              RunFilterETWSessionByURLCallback(testing::Eq(isolate),
                                               testing::Eq(origin_filter)))
      .Times(3);
  EXPECT_CALL(etw_isolate_operations_mock,
              SetEtwCodeEventHandler(testing::Eq(isolate),
                                     testing::Eq(kJitCodeEventDefault)))
      .Times(2);
  EXPECT_CALL(etw_isolate_operations_mock,
              SetEtwCodeEventHandler(testing::Eq(isolate),
                                     testing::Eq(kJitCodeEventEnumExisting |
                                                 ETWJITInterface::kEtwRundown)))
      .Times(1);
  EXPECT_CALL(etw_isolate_operations_mock,
              ResetEtwCodeEventHandler(testing::Eq(isolate)))
      .Times(1);
  ON_CALL(etw_isolate_operations_mock,
          RunFilterETWSessionByURLCallback(testing::Eq(isolate),
                                           testing::Eq(origin_filter)))
      .WillByDefault(
          testing::Return<FilterETWSessionByURLResult>({true, true}));
  ON_CALL(etw_isolate_operations_mock,
          RequestInterrupt(testing::Eq(isolate), testing::_, testing::_))
      .WillByDefault(testing::Invoke(
          [&isolate_thread](Isolate* isolate, InterruptCallback callback,
                            void* data) {
            isolate_thread = std::thread([isolate, callback, data]() {
              callback(reinterpret_cast<v8::Isolate*>(isolate), data);
            });
          }));

  EVENT_FILTER_DESCRIPTOR event_filter_descriptor;
  struct SchematizedTestFilter : public EVENT_FILTER_HEADER {
    char data[0];
  };

  size_t schematized_test_filter_size =
      sizeof(SchematizedTestFilter) + sizeof(origin_filter) - 1 /*remove '\0'*/;

  std::unique_ptr<SchematizedTestFilter> schematized_test_filter;
  schematized_test_filter.reset(reinterpret_cast<SchematizedTestFilter*>(
      new unsigned char[schematized_test_filter_size]));
  std::memset(schematized_test_filter.get(), 0 /*fill*/,
              schematized_test_filter_size);
  std::memcpy(schematized_test_filter->data, origin_filter,
              sizeof(origin_filter) - 1 /*remove '\0'*/);
  schematized_test_filter->Size =
      static_cast<ULONG>(schematized_test_filter_size);

  event_filter_descriptor.Ptr =
      reinterpret_cast<ULONGLONG>(schematized_test_filter.get());
  event_filter_descriptor.Type = EVENT_FILTER_TYPE_SCHEMATIZED;
  event_filter_descriptor.Size =
      static_cast<ULONG>(schematized_test_filter_size);

  ETWEnableCallback(&v8_etw_guid, kEtwControlEnable, /*level*/ 5,
                    /*match_any_keyword*/ ~0, /*match_all_keyword*/ 0,
                    /*filter_data*/ &event_filter_descriptor,
                    /*callback_context*/ nullptr);
  ASSERT_TRUE(has_active_etw_tracing_session_or_custom_filter);
  isolate_thread.join();
  ASSERT_TRUE(isolate->interpreted_frames_native_stack());

  ETWEnableCallback(&v8_etw_guid, kEtwControlCaptureState, /*level*/ 5,
                    /*match_any_keyword*/ ~0, /*match_all_keyword*/ 0,
                    /*filter_data*/ &event_filter_descriptor,
                    /*callback_context*/ nullptr);
  isolate_thread.join();

  ETWEnableCallback(&v8_etw_guid, kEtwControlEnable, /*level*/ 5,
                    /*match_any_keyword*/ ~0, /*match_all_keyword*/ 0,
                    /*filter_data*/ &event_filter_descriptor,
                    /*callback_context*/ nullptr);
  isolate_thread.join();

  ETWEnableCallback(&v8_etw_guid, kEtwControlDisable, /*level*/ 5,
                    /*match_any_keyword*/ ~0, /*match_all_keyword*/ 0,
                    /*filter_data*/ nullptr, /*callback_context*/ nullptr);
  ASSERT_FALSE(has_active_etw_tracing_session_or_custom_filter);
  isolate_thread.join();

  EtwIsolateOperations::SetInstanceForTesting(nullptr);
}

TEST_F(EtwControlTest, EnableWithNonMatchingFilterData) {
  v8_flags.enable_etw_stack_walking = true;

  // Set the flag below for helpful debug spew
  // v8_flags.etw_trace_debug = true;

  testing::NiceMock<EtwIsolateOperationsMock> etw_isolate_operations_mock;
  EtwIsolateOperations::SetInstanceForTesting(&etw_isolate_operations_mock);

  Isolate* isolate = reinterpret_cast<Isolate*>(this->isolate());
  IsolateLoadScriptData::AddIsolate(isolate);

  std::thread isolate_thread;
  constexpr char origin_filter[] =
      "{\"version\": 2.0, \"description\": \"\", \"filtered_urls\": "
      "[\"https://.*example.com\"], \"trace_interpreter_frames\": true}";
  EXPECT_CALL(etw_isolate_operations_mock,
              RunFilterETWSessionByURLCallback(testing::Eq(isolate),
                                               testing::Eq(origin_filter)))
      .Times(3);
  EXPECT_CALL(etw_isolate_operations_mock,
              SetEtwCodeEventHandler(testing::Eq(isolate),
                                     testing::Eq(kJitCodeEventEnumExisting)))
      .Times(0);
  EXPECT_CALL(etw_isolate_operations_mock,
              ResetEtwCodeEventHandler(testing::Eq(isolate)))
      .Times(1);
  ON_CALL(etw_isolate_operations_mock,
          RequestInterrupt(testing::Eq(isolate), testing::_, testing::_))
      .WillByDefault(testing::Invoke(
          [&isolate_thread](Isolate* isolate, InterruptCallback callback,
                            void* data) {
            isolate_thread = std::thread([isolate, callback, data]() {
              callback(reinterpret_cast<v8::Isolate*>(isolate), data);
            });
          }));

  EVENT_FILTER_DESCRIPTOR event_filter_descriptor;
  struct SchematizedTestFilter : public EVENT_FILTER_HEADER {
    char data[0];
  };

  size_t schematized_test_filter_size =
      sizeof(SchematizedTestFilter) + sizeof(origin_filter) - 1 /*remove '\0'*/;

  std::unique_ptr<SchematizedTestFilter> schematized_test_filter;
  schematized_test_filter.reset(reinterpret_cast<SchematizedTestFilter*>(
      new unsigned char[schematized_test_filter_size]));
  std::memset(schematized_test_filter.get(), 0 /*fill*/,
              schematized_test_filter_size);
  std::memcpy(schematized_test_filter->data, origin_filter,
              sizeof(origin_filter) - 1 /*remove '\0'*/);
  schematized_test_filter->Size =
      static_cast<ULONG>(schematized_test_filter_size);

  event_filter_descriptor.Ptr =
      reinterpret_cast<ULONGLONG>(schematized_test_filter.get());
  event_filter_descriptor.Type = EVENT_FILTER_TYPE_SCHEMATIZED;
  event_filter_descriptor.Size =
      static_cast<ULONG>(schematized_test_filter_size);

  ETWEnableCallback(&v8_etw_guid, kEtwControlEnable, /*level*/ 5,
                    /*match_any_keyword*/ ~0, /*match_all_keyword*/ 0,
                    /*filter_data*/ &event_filter_descriptor,
                    /*callback_context*/ nullptr);
  ASSERT_TRUE(has_active_etw_tracing_session_or_custom_filter);
  isolate_thread.join();
  ASSERT_FALSE(isolate->interpreted_frames_native_stack());

  ETWEnableCallback(&v8_etw_guid, kEtwControlCaptureState, /*level*/ 5,
                    /*match_any_keyword*/ ~0, /*match_all_keyword*/ 0,
                    /*filter_data*/ &event_filter_descriptor,
                    /*callback_context*/ nullptr);
  isolate_thread.join();

  ETWEnableCallback(&v8_etw_guid, kEtwControlEnable, /*level*/ 5,
                    /*match_any_keyword*/ ~0, /*match_all_keyword*/ 0,
                    /*filter_data*/ &event_filter_descriptor,
                    /*callback_context*/ nullptr);
  isolate_thread.join();

  ETWEnableCallback(&v8_etw_guid, kEtwControlDisable, /*level*/ 5,
                    /*match_any_keyword*/ ~0, /*match_all_keyword*/ 0,
                    /*filter_data*/ nullptr, /*callback_context*/ nullptr);
  ASSERT_FALSE(has_active_etw_tracing_session_or_custom_filter);
  isolate_thread.join();

  EtwIsolateOperations::SetInstanceForTesting(nullptr);
}

TEST_F(EtwControlTest, EnableWithCustomFilterOnly) {
  v8_flags.enable_etw_stack_walking = false;
  v8_flags.enable_etw_by_custom_filter_only = true;

  // Set the flag below for helpful debug spew
  // v8_flags.etw_trace_debug = true;

  testing::NiceMock<EtwIsolateOperationsMock> etw_isolate_operations_mock;
  EtwIsolateOperations::SetInstanceForTesting(&etw_isolate_operations_mock);

  Isolate* isolate = reinterpret_cast<Isolate*>(this->isolate());
  IsolateLoadScriptData::AddIsolate(isolate);

  std::thread isolate_thread;
  constexpr char origin_filter[] =
      "{\"version\": 2.0, \"description\": \"\", \"filtered_urls\": "
      "[\"https://.*example.com\"], \"trace_interpreter_frames\": true}";
  EXPECT_CALL(etw_isolate_operations_mock,
              RunFilterETWSessionByURLCallback(testing::Eq(isolate),
                                               testing::Eq(origin_filter)))
      .Times(3);
  EXPECT_CALL(etw_isolate_operations_mock,
              SetEtwCodeEventHandler(testing::Eq(isolate),
                                     testing::Eq(kJitCodeEventDefault)))
      .Times(2);
  EXPECT_CALL(etw_isolate_operations_mock,
              SetEtwCodeEventHandler(testing::Eq(isolate),
                                     testing::Eq(kJitCodeEventEnumExisting |
                                                 ETWJITInterface::kEtwRundown)))
      .Times(1);
  EXPECT_CALL(etw_isolate_operations_mock,
              ResetEtwCodeEventHandler(testing::Eq(isolate)))
      .Times(1);
  ON_CALL(etw_isolate_operations_mock,
          RunFilterETWSessionByURLCallback(testing::Eq(isolate),
                                           testing::Eq(origin_filter)))
      .WillByDefault(
          testing::Return<FilterETWSessionByURLResult>({true, true}));
  ON_CALL(etw_isolate_operations_mock,
          RequestInterrupt(testing::Eq(isolate), testing::_, testing::_))
      .WillByDefault(testing::Invoke(
          [&isolate_thread](Isolate* isolate, InterruptCallback callback,
                            void* data) {
            isolate_thread = std::thread([isolate, callback, data]() {
              callback(reinterpret_cast<v8::Isolate*>(isolate), data);
            });
          }));

  EVENT_FILTER_DESCRIPTOR event_filter_descriptor;
  struct SchematizedTestFilter : public EVENT_FILTER_HEADER {
    char data[0];
  };

  size_t schematized_test_filter_size =
      sizeof(SchematizedTestFilter) + sizeof(origin_filter) - 1 /*remove '\0'*/;

  std::unique_ptr<SchematizedTestFilter> schematized_test_filter;
  schematized_test_filter.reset(reinterpret_cast<SchematizedTestFilter*>(
      new unsigned char[schematized_test_filter_size]));
  std::memset(schematized_test_filter.get(), 0 /*fill*/,
              schematized_test_filter_size);
  std::memcpy(schematized_test_filter->data, origin_filter,
              sizeof(origin_filter) - 1 /*remove '\0'*/);
  schematized_test_filter->Size =
      static_cast<ULONG>(schematized_test_filter_size);

  event_filter_descriptor.Ptr =
      reinterpret_cast<ULONGLONG>(schematized_test_filter.get());
  event_filter_descriptor.Type = EVENT_FILTER_TYPE_SCHEMATIZED;
  event_filter_descriptor.Size =
      static_cast<ULONG>(schematized_test_filter_size);

  ETWEnableCallback(&v8_etw_guid, kEtwControlEnable, /*level*/ 5,
                    /*match_any_keyword*/ ~0, /*match_all_keyword*/ 0,
                    /*filter_data*/ &event_filter_descriptor,
                    /*callback_context*/ nullptr);
  ASSERT_TRUE(has_active_etw_tracing_session_or_custom_filter);
  isolate_thread.join();
  ASSERT_TRUE(isolate->interpreted_frames_native_stack());

  ETWEnableCallback(&v8_etw_guid, kEtwControlCaptureState, /*level*/ 5,
                    /*match_any_keyword*/ ~0, /*match_all_keyword*/ 0,
                    /*filter_data*/ &event_filter_descriptor,
                    /*callback_context*/ nullptr);
  isolate_thread.join();

  ETWEnableCallback(&v8_etw_guid, kEtwControlEnable, /*level*/ 5,
                    /*match_any_keyword*/ ~0, /*match_all_keyword*/ 0,
                    /*filter_data*/ &event_filter_descriptor,
                    /*callback_context*/ nullptr);
  isolate_thread.join();

  ETWEnableCallback(&v8_etw_guid, kEtwControlDisable, /*level*/ 5,
                    /*match_any_keyword*/ ~0, /*match_all_keyword*/ 0,
                    /*filter_data*/ nullptr, /*callback_context*/ nullptr);
  ASSERT_FALSE(has_active_etw_tracing_session_or_custom_filter);
  isolate_thread.join();

  EtwIsolateOperations::SetInstanceForTesting(nullptr);
}

TEST_F(EtwControlTest, EnableWithNonMatchingCustomFilterOnly) {
  v8_flags.enable_etw_stack_walking = false;
  v8_flags.enable_etw_by_custom_filter_only = true;

  // Set the flag below for helpful debug spew
  // v8_flags.etw_trace_debug = true;

  testing::NiceMock<EtwIsolateOperationsMock> etw_isolate_operations_mock;
  EtwIsolateOperations::SetInstanceForTesting(&etw_isolate_operations_mock);

  Isolate* isolate = reinterpret_cast<Isolate*>(this->isolate());
  IsolateLoadScriptData::AddIsolate(isolate);

  std::thread isolate_thread;
  constexpr char origin_filter[] =
      "{\"version\": 2.0, \"description\": \"\", \"filtered_urls\": "
      "[\"https://.*example.com\"], \"trace_interpreter_frames\": true}";
  EXPECT_CALL(etw_isolate_operations_mock,
              RunFilterETWSessionByURLCallback(testing::Eq(isolate),
                                               testing::Eq(origin_filter)))
      .Times(1);
  EXPECT_CALL(etw_isolate_operations_mock,
              SetEtwCodeEventHandler(testing::Eq(isolate),
                                     testing::Eq(kJitCodeEventEnumExisting)))
      .Times(0);
  ON_CALL(etw_isolate_operations_mock,
          RunFilterETWSessionByURLCallback(testing::Eq(isolate),
                                           testing::Eq(origin_filter)))
      .WillByDefault(
          testing::Return<FilterETWSessionByURLResult>({false, true}));
  ON_CALL(etw_isolate_operations_mock,
          RequestInterrupt(testing::Eq(isolate), testing::_, testing::_))
      .WillByDefault(testing::Invoke(
          [&isolate_thread](Isolate* isolate, InterruptCallback callback,
                            void* data) {
            isolate_thread = std::thread([isolate, callback, data]() {
              callback(reinterpret_cast<v8::Isolate*>(isolate), data);
            });
          }));

  EVENT_FILTER_DESCRIPTOR event_filter_descriptor;
  struct SchematizedTestFilter : public EVENT_FILTER_HEADER {
    char data[0];
  };

  size_t schematized_test_filter_size =
      sizeof(SchematizedTestFilter) + sizeof(origin_filter) - 1 /*remove '\0'*/;

  std::unique_ptr<SchematizedTestFilter> schematized_test_filter;
  schematized_test_filter.reset(reinterpret_cast<SchematizedTestFilter*>(
      new unsigned char[schematized_test_filter_size]));
  std::memset(schematized_test_filter.get(), 0 /*fill*/,
              schematized_test_filter_size);
  std::memcpy(schematized_test_filter->data, origin_filter,
              sizeof(origin_filter) - 1 /*remove '\0'*/);
  schematized_test_filter->Size =
      static_cast<ULONG>(schematized_test_filter_size);

  event_filter_descriptor.Ptr =
      reinterpret_cast<ULONGLONG>(schematized_test_filter.get());
  event_filter_descriptor.Type = EVENT_FILTER_TYPE_SCHEMATIZED;
  event_filter_descriptor.Size =
      static_cast<ULONG>(schematized_test_filter_size);

  ETWEnableCallback(&v8_etw_guid, kEtwControlEnable, /*level*/ 5,
                    /*match_any_keyword*/ ~0, /*match_all_keyword*/ 0,
                    /*filter_data*/ &event_filter_descriptor,
                    /*callback_context*/ nullptr);
  isolate_thread.join();
  ASSERT_TRUE(has_active_etw_tracing_session_or_custom_filter);
  ASSERT_FALSE(isolate->interpreted_frames_native_stack());

  ETWEnableCallback(&v8_etw_guid, kEtwControlDisable, /*level*/ 5,
                    /*match_any_keyword*/ ~0, /*match_all_keyword*/ 0,
                    /*filter_data*/ nullptr, /*callback_context*/ nullptr);
  isolate_thread.join();
  ASSERT_FALSE(has_active_etw_tracing_session_or_custom_filter);
  ASSERT_FALSE(isolate->interpreted_frames_native_stack());

  EtwIsolateOperations::SetInstanceForTesting(nullptr);
}

}  // namespace ETWJITInterface
}  // namespace internal
}  // namespace v8
