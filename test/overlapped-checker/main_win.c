#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include <windows.h>

static char buf[256];
static DWORD read_count;
static HANDLE stdin_h;
static OVERLAPPED stdin_o;

static void die(const char* buf) {
  fprintf(stderr, "%s\n", buf);
  fflush(stderr);
  exit(100);
}

static void overlapped_read(void) {
  if (ReadFile(stdin_h, buf, sizeof(buf), NULL, &stdin_o)) {
    // Since we start the read operation immediately before requesting a write,
    // it should never complete synchronously since no data would be available
    die("read completed synchronously");
  }
  if (GetLastError() != ERROR_IO_PENDING) {
    die("overlapped read failed");
  }
}

static void write(const char* buf, size_t buf_size) {
  overlapped_read();
  DWORD write_count;
  HANDLE stdout_h = GetStdHandle(STD_OUTPUT_HANDLE);
  if (!WriteFile(stdout_h, buf, buf_size, &write_count, NULL)) {
    die("overlapped write failed");
  }
  fprintf(stderr, "%ld", write_count);
  fflush(stderr);
}

int main(void) {
  HANDLE event = CreateEvent(NULL, FALSE, FALSE, NULL);
  if (event == NULL) {
    die("failed to create event handle");
  }

  stdin_h = GetStdHandle(STD_INPUT_HANDLE);
  stdin_o.hEvent = event;

  write("0", 1);

  while (1) {
    DWORD result = WaitForSingleObject(event, INFINITE);
    if (result == WAIT_OBJECT_0) {
      if (!GetOverlappedResult(stdin_h, &stdin_o, &read_count, FALSE)) {
        die("failed to get overlapped read result");
      }
      if (strncmp(buf, "exit", read_count) == 0) {
        break;
      }
      write(buf, read_count);
    } else {
      char emsg[0xfff];
      int ecode = GetLastError();
      DWORD rv = FormatMessage(
          FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
          NULL,
          ecode,
          MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
          (LPSTR)emsg,
          sizeof(emsg),
          NULL);
      if (rv > 0) {
        snprintf(emsg, sizeof(emsg),
            "WaitForSingleObject failed. Error %d (%s)", ecode, emsg);
      } else {
        snprintf(emsg, sizeof(emsg),
            "WaitForSingleObject failed. Error %d", ecode);
      }
      die(emsg);
    }
  }

  return 0;
}
