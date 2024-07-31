#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

int main() {
  const char* target = "./input-in-subdir.txt";
  const char* linkpath = "/sandbox/subdir/test_link";
  char readlink_result[128];
  size_t result_size = sizeof(readlink_result);

  assert(0 == symlink(target, linkpath));
  assert(readlink(linkpath, readlink_result, result_size) ==
         strlen(target));
  assert(0 == strcmp(readlink_result, target));

  FILE* file = fopen(linkpath, "r");
  assert(file != NULL);

  int c = fgetc(file);
  while (c != EOF) {
    int wrote = fputc(c, stdout);
    assert(wrote != EOF);
    c = fgetc(file);
  }
}
