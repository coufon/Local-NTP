#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

int main(int argc, char **argv) {
  int fd;
  if ((fd = open("test.txt", O_RDONLY)) < 0) {
    perror("open");  
  }
  long long val;
  int r;
  r = read(fd, &val, sizeof(long long));
  printf("%d %lld\n", r, val);
  r = read(fd, &val, sizeof(long long));
  printf("%d %lld\n", r, val);
  r = read(fd, &val, sizeof(long long));
  printf("%d %lld\n", r, val);
  r = read(fd, &val, sizeof(long long));
  printf("%d %lld\n", r, val);
  r = read(fd, &val, sizeof(long long));
  printf("%d %lld\n", r, val);
  
  return 0;
}
