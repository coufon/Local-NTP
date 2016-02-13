/*
 * Local NTP (LNTP)
 * Author: Zhou Fang (zhoufang@ucsd.edu)
 * Reference: 
 * 1. NTPv4: https://www.eecis.udel.edu/~mills/database/reports/ntp4/ntp4.pdf
 * 2. Use some source code from: http://blog.csdn.net/rich_baba/article/details/6052863
 */

#include "ntpclient.h"

NtpConfig NtpCfg; // configuration variable
Response resp_list[NSTAGE];
int total  = 0;
int cursor = 0;
long long *mapped;
FILE *fp_result;


int main(int argc, char **argv){
  int sock;
  int ret;
  Response resp;
  int addr_len;
  
  if( (fp_result = fopen(FILE_PATH, "a+")) == NULL ){
    perror(FILE_PATH);
    exit(1);
  }
  
  load_default_cfg(&NtpCfg); // load configuration file

  // init clock model
  struct timeval tv_real;
  struct timespec ts_raw;
  long long t_real, t_raw;
  local_clock.index = 0;
  local_clock.ratio = 1;
  local_clock.period = DEF_PSEC;

  gettimeofday(&tv_real, NULL); // real time clock
  clock_gettime(CLOCK_MONOTONIC_RAW, &ts_raw);
  
  local_clock.base_y = TTLUSEC(tv_real.tv_sec, tv_real.tv_usec);
  local_clock.base_x = TTLUSEC(ts_raw.tv_sec, ts_raw.tv_nsec/1000); 
  
  /*
  // test clock model
  gettimeofday(&tv_real, NULL); // real time clock
  t_real = TTLUSEC(tv_real.tv_sec, tv_real.tv_usec);
  
  long long t_predict = now();
  printf("Test clock model: predict %lld, clock %lld\n", t_predict, t_real);  
  */

  // ============== prepare mmap =============
  struct stat sb;
  int fd;
  if ((fd = open("mmap/clock.txt", O_RDWR | O_CREAT, 0777)) < 0) {
    perror("open");  
  }
  if ((fstat(fd, &sb)) == -1) {  
    perror("fstat");  
  }  
  if ((mapped = (long long *)mmap(NULL, sb.st_size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0)) == (void *)-1) {  
    perror("mmap");  
  }
  close(fd);  
  mapped[0] = 0; // init index
  
  // =================== NTP =================
  int i = 0, j = 0;
  sock = ntp_conn_server(NtpCfg.servaddr[0], NtpCfg.port); // connect to npt server

#ifdef DEBUG
  printf("\n");
#endif  
	
  //while(i < NUM_SAMPLE){
  while(true){
    i++;
    total = NTP_SAMPLE;
    for(j = 0; j < NTP_SAMPLE; j++){ // poll all servers
      send_packet(sock); // send ntp package
      if(get_server_time(sock, &resp) == false){
	total --;
	if(total < MIN_NTP_SAMPLE) {
	  clock_gettime(CLOCK_MONOTONIC_RAW, &ts_raw);
	  t_raw = TTLUSEC(ts_raw.tv_sec, ts_raw.tv_nsec/1000);
	  local_clock.base_y = local_clock.ratio*(t_raw - local_clock.base_x) + local_clock.base_y;
	  local_clock.base_x = t_raw;
	  local_clock.ratio = 1;
	  printf("Quit this NTP sync\n");
	  break; // quit this sync
	}
      } else {
	//fprintf(fp_result, "%d %lld %lld %lld %lld\n", j, resp.t1, resp.t2, resp.t3, resp.t4);
	// process here:
	resp_list[cursor++] = resp;
      }
#ifdef DEBUG
      printf("cursor: %d, total: %d\n\n", cursor, total);
#endif
      if(cursor == total) {
	cursor = 0;
	ntp_process(); // process responses
      }
    }
    sleep(NtpCfg.psec); // ntp check interval
  }
  //fclose(fp_result);
  for(j = 0; j < NR_REMOTE; j++){
    close(sock);
  }
  exit(0);
}
