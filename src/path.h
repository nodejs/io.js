#ifndef SRC_PATH_H_
#define SRC_PATH_H_

#if defined(NODE_WANT_INTERNALS) && NODE_WANT_INTERNALS

#include <string>
#include <vector>
#include "node_options-inl.h"
#include "util-inl.h"

namespace node {

constexpr bool IsPathSeparator(const char c) noexcept;

std::string NormalizeString(const std::string_view path,
                            bool allowAboveRoot,
                            const std::string_view separator);

std::string PathResolve(Environment* env,
                        const std::vector<std::string_view>& paths);

#ifdef _WIN32
constexpr bool IsWindowsDeviceRoot(const char c) noexcept;
#endif  // _WIN32

void ToNamespacedPath(Environment* env, BufferValue* path);
void FromNamespacedPath(std::string* path);

}  // namespace node

#endif  // defined(NODE_WANT_INTERNALS) && NODE_WANT_INTERNALS

#endif  // SRC_PATH_H_
