/*
 * Local NTP (LNTP)
 * Author: Zhou Fang (zhoufang@ucsd.edu)
 * Reference: 
 * 1. NTPv4: https://www.eecis.udel.edu/~mills/database/reports/ntp4/ntp4.pdf
 * 2. Use some source code from: http://blog.csdn.net/rich_baba/article/details/6052863
 */

#include   <stdio.h>
#include   <stdlib.h>
#include   <string.h>
#include   <stdarg.h>
#include   <unistd.h>
#include   <netinet/in.h>
#include   <sys/socket.h>
#include   <sys/types.h>
#include   <arpa/inet.h>
#include   <netdb.h>
#include   <sys/time.h>
#include   <time.h>
#include   <sys/select.h>
#include   <stdbool.h>
#include   <signal.h>
#include   <sys/param.h>
#include   <sys/stat.h>
#include   <fcntl.h>
#include   <errno.h>
#include   <sys/mman.h>  

#include <math.h>
//#include <pthread.h>
#include "ntpconfig.h"

#define  JAN_1970   0x83aa7e80      //3600s*24h*(365days*70years+17days)
#define  MILLION 1000000

#define  NTPFRAC(x) (4294 * (x) + ((1981 * (x)) >> 11))  
#define  USEC(x) (((x) >> 12) - 759 * ((((x) >> 10) + 32768) >> 16))
#define  MKSEC(ntpt)   ((ntpt).integer - JAN_1970)
#define  MKUSEC(ntpt)  (USEC((ntpt).fraction))
#define  TTLUSEC(sec, usec) ((long long)(sec)*1000000 + (usec))
#define  GETSEC(us)    ((us)/1000000) 
#define  GETUSEC(us)   ((us)%1000000)
#define  DATA(i) ntohl((( unsigned   int  *)data)[i])
#define SQUARE(x) (x*x)
#define LOG2D(a) ( (a) < 0 ? 1. / (1L << -(a)) : 1L << (a) )
#define INT8(a) ( a&128 ? a - 256 : a )

// ntp package send head
#define  LI 0          // protocol head elements
#define  VN 4          // version
#define  MODE 3        // mode: client request
#define  STRATUM 0
#define  POLL 4        // maximal interval between requests

#define  DEF_NTP_PORT 8000  // NTP port (normal NTP uses 123)

typedef struct{
  //char servaddr[256];
  char servaddr[NR_REMOTE][256]; // 5 ntp servers
  unsigned int port;
  int psec;
  int pmin;
  int phour;
  int timeout;
  //bool logen;
  //char logpath[256];
} NtpConfig;

//ntp timestamp structure
typedef struct{
  unsigned   int  integer;
  unsigned   int  fraction;
} NtpTime;

// round-trip timestamps
typedef struct{
  int stratum, precision;
  long long rootDelay, rootDisp;
  long long delay, offset, disp;
} Response;

// ============ clock select ============
typedef struct{
  int stratum;
  long long offset, low, up, rootDist; 
} Interval;

typedef struct{
  int type;
  long long offset;
} Point;

typedef struct{
  int stratum;
  long long offset, rootDist, weight; 
} Survivor;

typedef struct{
  // clock model: real = ratio*(raw - base) + base + delta
  double ratio;
  long long base_x, base_y;
  long long low, up, offset, jitter;
  long long index, period; // init to 0, increase upon updating
} Clock;

int log_record( char *record, ...);

void ntp_process();
extern Response resp_list[NSTAGE];
extern int total;
extern int cursor;
extern Clock local_clock;
extern long long *mapped;
extern FILE *fp_result;

unsigned long longnative_read_tsc(void);
long long gettime();
void reset_ratio();
