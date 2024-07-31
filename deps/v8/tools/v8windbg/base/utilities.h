// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_TOOLS_V8WINDBG_BASE_UTILITIES_H_
#define V8_TOOLS_V8WINDBG_BASE_UTILITIES_H_

#include "tools/v8windbg/base/dbgext.h"

inline const wchar_t* U16ToWChar(const char16_t* p_u16) {
  static_assert(sizeof(wchar_t) == sizeof(char16_t), "wrong wchar size");
  return reinterpret_cast<const wchar_t*>(p_u16);
}

inline const wchar_t* U16ToWChar(std::u16string& str) {
  return U16ToWChar(str.data());
}

#if defined(WIN32)
inline std::u16string ConvertToU16String(std::string utf8_string) {
  int len_chars =
      ::MultiByteToWideChar(CP_UTF8, 0, utf8_string.c_str(), -1, nullptr, 0);

  char16_t* p_buff =
      static_cast<char16_t*>(malloc(len_chars * sizeof(char16_t)));

  // On Windows wchar_t is the same a char16_t
  static_assert(sizeof(wchar_t) == sizeof(char16_t), "wrong wchar size");
  len_chars =
      ::MultiByteToWideChar(CP_UTF8, 0, utf8_string.c_str(), -1,
                            reinterpret_cast<wchar_t*>(p_buff), len_chars);
  std::u16string result{p_buff};
  free(p_buff);

  return result;
}

inline std::string ConvertFromU16String(std::u16string u16string) {
  int len_chars =
      ::WideCharToMultiByte(CP_UTF8, 0, U16ToWChar(u16string.c_str()), -1,
                            nullptr, 0, nullptr, nullptr);

  char* p_buff = static_cast<char*>(malloc(len_chars * sizeof(char)));

  len_chars = ::WideCharToMultiByte(CP_UTF8, 0, U16ToWChar(u16string.c_str()),
                                    -1, p_buff, len_chars, nullptr, nullptr);
  std::string result{p_buff};
  free(p_buff);

  return result;
}

#else
#error String encoding conversion must be provided for the target platform.
#endif

HRESULT CreateProperty(IDataModelManager* p_manager,
                       IModelPropertyAccessor* p_property,
                       IModelObject** pp_property_object);

HRESULT CreateMethod(IDataModelManager* p_manager, IModelMethod* p_method,
                     IModelObject** pp_method_object);

HRESULT UnboxProperty(IModelObject* object, IModelPropertyAccessor** result);

HRESULT CreateTypedIntrinsic(uint64_t value, IDebugHostType* type,
                             IModelObject** result);

HRESULT CreateULong64(ULONG64 value, IModelObject** pp_int);

HRESULT UnboxULong64(IModelObject* object, ULONG64* value,
                     bool convert = false);

HRESULT GetInt32(IDebugHostConstant* object, int* value);

HRESULT CreateInt32(int value, IModelObject** pp_int);

HRESULT CreateUInt32(uint32_t value, IModelObject** pp_int);

HRESULT CreateBool(bool value, IModelObject** pp_val);

HRESULT CreateNumber(double value, IModelObject** pp_val);

HRESULT CreateString(std::u16string value, IModelObject** pp_val);

HRESULT UnboxString(IModelObject* object, BSTR* value);

HRESULT GetModelAtIndex(WRL::ComPtr<IModelObject>& sp_parent,
                        WRL::ComPtr<IModelObject>& sp_index,
                        IModelObject** p_result);

HRESULT GetCurrentThread(WRL::ComPtr<IDebugHostContext>& sp_host_context,
                         IModelObject** p_current_thread);

#define RETURN_IF_FAIL(expression) \
  do {                             \
    HRESULT hr = expression;       \
    if (FAILED(hr)) {              \
      return hr;                   \
    }                              \
  } while (false)

#endif  // V8_TOOLS_V8WINDBG_BASE_UTILITIES_H_
