#include "absl/time/clock.h"

#include <sys/time.h>
#include <ctime>
#include <cstdint>

#include "absl/base/internal/raw_logging.h"

namespace absl {
ABSL_NAMESPACE_BEGIN
namespace time_internal {

static int64_t GetCurrentTimeNanosFromSystem() {
  const int64_t kNanosPerSecond = 1000 * 1000 * 1000;
  struct timespec ts;
  ABSL_RAW_CHECK(clock_gettime(CLOCK_REALTIME, &ts) == 0,
                 "Failed to read real-time clock.");
  return (int64_t{ts.tv_sec} * kNanosPerSecond +
          int64_t{ts.tv_nsec});
}

}  // namespace time_internal
ABSL_NAMESPACE_END
}  // namespace absl
