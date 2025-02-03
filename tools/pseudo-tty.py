#!/usr/bin/env python3

import errno
import os
import pty
import select
import signal
import sys
import termios

STDIN = 0
STDOUT = 1
STDERR = 2


def pipe(sfd, dfd):
  try:
    data = os.read(sfd, 256)
  except OSError as e:
    if e.errno != errno.EIO:
      raise
    return True  # EOF

  if not data:
    return True  # EOF

  if dfd == STDOUT:
    # Work around platform quirks. Some platforms echo ^D as \x04
    # (AIX, BSDs) and some don't (Linux).
    #
    # The comparison against b' '[0] is because |data| is
    # a string in python2 but a bytes object in python3.
    filt = lambda c: c >= b' '[0] or c in b'\t\n\r\f'
    data = filter(filt, data)
    data = bytes(data)

  while data:
    try:
      n = os.write(dfd, data)
    except OSError as e:
      if e.errno != errno.EIO:
        raise
      return True  # EOF
    data = data[n:]


if __name__ == '__main__':
  argv = sys.argv[1:]

  # Make select() interruptible by SIGCHLD.
  signal.signal(signal.SIGCHLD, lambda nr, _: None)

  parent_fd, child_fd = pty.openpty()
  assert parent_fd > STDIN

  mode = termios.tcgetattr(child_fd)
  # Don't translate \n to \r\n.
  mode[1] = mode[1] & ~termios.ONLCR  # oflag
  # Disable ECHOCTL. It's a BSD-ism that echoes e.g. \x04 as ^D but it
  # doesn't work on platforms like AIX and Linux. I checked Linux's tty
  # driver and it's a no-op, the driver is just oblivious to the flag.
  mode[3] = mode[3] & ~termios.ECHOCTL  # lflag
  termios.tcsetattr(child_fd, termios.TCSANOW, mode)

  pid = os.fork()
  if not pid:
    os.setsid()
    os.close(parent_fd)

    # Ensure the pty is a controlling tty.
    name = os.ttyname(child_fd)
    fd = os.open(name, os.O_RDWR)
    os.dup2(fd, child_fd)
    os.close(fd)

    os.dup2(child_fd, STDIN)
    os.dup2(child_fd, STDOUT)
    os.dup2(child_fd, STDERR)

    if child_fd > STDERR:
      os.close(child_fd)

    os.execve(argv[0], argv, os.environ)
    raise Exception('unreachable')

  os.close(child_fd)

  fds = [STDIN, parent_fd]
  while fds:
    try:
      rfds, _, _ = select.select(fds, [], [])
    except select.error as e:
      if e[0] != errno.EINTR:
        raise
      if pid == os.waitpid(pid, os.WNOHANG)[0]:
        break

    if STDIN in rfds:
      if pipe(STDIN, parent_fd):
        fds.remove(STDIN)

    if parent_fd in rfds:
      if pipe(parent_fd, STDOUT):
        break
