#include <assert.h>
#include <poll.h>
#include <time.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>

int main(void) {
  struct pollfd fds[4];
  time_t before, now;
  int ret;
  char* platform;
  int is_aix_or_os400;
  int is_win;

  platform = getenv("NODE_PLATFORM");
  is_aix_or_os400 = platform != NULL && (0 == strcmp(platform, "aix") || 0 == strcmp(platform, "os400"));
  is_win = platform != NULL && 0 == strcmp(platform, "win32");

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

  // V8 has a bug that allows unsupported parts of this test to run,
  // causing the test to fail. poll_win.c is a workaround.
  // https://github.com/nodejs/node/issues/51822
  // The rest of the test is unsupported on Windows.
  if (is_win)
    return 0;

  fds[0] = (struct pollfd){.fd = 1, .events = POLLOUT, .revents = 0};
  fds[1] = (struct pollfd){.fd = 2, .events = POLLOUT, .revents = 0};

  ret = poll(fds, 2, -1);
  assert(ret == 2);
  assert(fds[0].revents == POLLOUT);
  assert(fds[1].revents == POLLOUT);

  // Make a poll() call with duplicate file descriptors.
  fds[0] = (struct pollfd){.fd = 1, .events = POLLOUT, .revents = 0};
  fds[1] = (struct pollfd){.fd = 2, .events = POLLOUT, .revents = 0};
  fds[2] = (struct pollfd){.fd = 1, .events = POLLOUT, .revents = 0};
  fds[3] = (struct pollfd){.fd = 1, .events = POLLIN, .revents = 0};

  ret = poll(fds, 2, -1);
  assert(ret == 2);
  assert(fds[0].revents == POLLOUT);
  assert(fds[1].revents == POLLOUT);
  assert(fds[2].revents == 0);
  assert(fds[3].revents == 0);

  // The original version of this test expected a timeout and return value of
  // zero. In the Node test suite, STDIN is not a TTY, and poll() returns one,
  // with revents = POLLHUP | POLLIN, except on AIX whose poll() does not
  // support POLLHUP.
  fds[0] = (struct pollfd){.fd = 0, .events = POLLIN, .revents = 0};
  ret = poll(fds, 1, 2000);
  assert(ret == 1);

  if (is_aix_or_os400)
    assert(fds[0].revents == POLLIN);
  else
    assert(fds[0].revents == (POLLHUP | POLLIN));

  return 0;
}
