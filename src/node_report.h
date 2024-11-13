#ifndef SRC_NODE_REPORT_H_
#define SRC_NODE_REPORT_H_

#if defined(NODE_WANT_INTERNALS) && NODE_WANT_INTERNALS

#include "node.h"
#include "node_buffer.h"
#include "uv.h"
#include "util.h"

#ifndef _WIN32
#include <sys/types.h>
#include <unistd.h>
#endif

#include <iomanip>
#include <sstream>

namespace node {
namespace report {
// Function declarations - utility functions in src/node_report_utils.cc
void WalkHandleNetwork(uv_handle_t* h, void* arg);
void WalkHandleNoNetwork(uv_handle_t* h, void* arg);

template <typename T>
std::string ValueToHexString(T value) {
  std::stringstream hex;

  hex << "0x" << std::setfill('0') << std::setw(sizeof(T) * 2) << std::hex <<
    value;
  return hex.str();
}

// Function declarations - export functions in src/node_report_module.cc
void WriteReport(const v8::FunctionCallbackInfo<v8::Value>& info);
void GetReport(const v8::FunctionCallbackInfo<v8::Value>& info);

}  // namespace report
}  // namespace node

#endif  // defined(NODE_WANT_INTERNALS) && NODE_WANT_INTERNALS

#endif  // SRC_NODE_REPORT_H_
