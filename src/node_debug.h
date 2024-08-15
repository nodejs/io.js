#pragma once

#if defined(NODE_WANT_INTERNALS) && NODE_WANT_INTERNALS

#ifdef DEBUG
#include <string_view>
#endif  // DEBUG

namespace node {
namespace debug {

#ifdef DEBUG
void TrackV8FastApiCall(std::string_view key);
int GetV8FastApiCallCount(std::string_view key);

#define TRACK_V8_FAST_API_CALL(key) node::debug::TrackV8FastApiCall(key)
#else  // !DEBUG
#define TRACK_V8_FAST_API_CALL(key)
#endif  // DEBUG

}  // namespace debug
}  // namespace node

#endif  // defined(NODE_WANT_INTERNALS) && NODE_WANT_INTERNALS
