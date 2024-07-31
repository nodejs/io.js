#include <assert.h>
#include <poll.h>
#include <time.h>
#include <unistd.h>

int main(void) {
  struct pollfd fds[4];
  time_t before, now;
  int ret;

  // Test sleep() behavior.
  time(&before);
  sleep(1);
  time(&now);
  assert(now - before >= 1);

  // Test poll() timeout behavior.
  fds[0] = (struct pollfd){.fd = -1, .events = 0, .revents = 0};
  time(&before);
  ret = poll(fds, 1, 2000);
  time(&now);
  assert(ret == 0);
  assert(now - before >= 2);

  return 0;
}
