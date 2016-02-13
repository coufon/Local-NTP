#include <sys/mman.h>  
#include <sys/stat.h>  
#include <fcntl.h>  
#include <stdio.h>  
#include <stdlib.h>  
#include <unistd.h>  
#include <error.h>  

typedef struct{
  // clock model: real = ratio*(raw - base) + base + delta
  double ratio;
  long long base_x, base_y;
  long long low, up, offset, jitter;
  long long index, period; // init to 0, increase upon updating
} Clock;
  
int main(int argc, char **argv)  
{  
    Clock local;
    local.index = 1;
    local.offset = 100;
    local.base_x = 200;
    local.base_y = 300;
    local.low = 400;
    local.up = 500;
    local.period = 600;
    local.jitter = 700;
    
 
    int fd, nread, i;  
    struct stat sb;  
    long long *mapped;
  
    /* 打开文件 */  
    if ((fd = open("clock.txt", O_RDWR | O_CREAT, 0777)) < 0) {
        perror("open");  
    }

    int w1 = write(fd, &local.index,  sizeof(long long));
    int w2 = write(fd, &local.offset, sizeof(long long));
    int w3 = write(fd, &local.base_x, sizeof(long long));
    int w4 = write(fd, &local.base_y, sizeof(long long));
    int w5 = write(fd, &local.low,    sizeof(long long));
    int w6 = write(fd, &local.up,     sizeof(long long));
    int w7 = write(fd, &local.period, sizeof(long long));
    int w8 = write(fd, &local.jitter, sizeof(long long));
  
    printf("%d %d %d %d %d %d %d %d\n", w1, w2, w3, w4, w5, w6, w7, w8);

    return 0; 
}
