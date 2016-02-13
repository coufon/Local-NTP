#include "ntpclient.h"

Clock local_clock;

// default configuration
void load_default_cfg(NtpConfig *NtpCfg){
  strcpy(NtpCfg->servaddr[0], DEF_NTP_SERVER0);
  
  NtpCfg->port = DEF_NTP_PORT;
  NtpCfg->psec = DEF_PSEC;

  NtpCfg->timeout = DEF_TIMEOUT;
}

// ================= Connect to timer server ===============
int ntp_conn_server(const char *servname, int port)
{
  int sock;
  int addr_len = sizeof (struct sockaddr_in);
  struct sockaddr_in addr_src; //local socket
  struct sockaddr_in addr_dst; //server socket
    
  sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP); //UDP sockect 
  if(sock == -1){
    printf("sock creation fails, force to exit!\n");
    exit(1);
  }
  
  // > 1000ms delay, disgard
  struct timeval tv;
  tv.tv_sec = DEF_TIMEOUT;
  tv.tv_usec = 0;
  if (setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)) < 0) {
    perror("Error");
  }
  
  memset(&addr_src, 0, addr_len);
  addr_src.sin_family      = AF_INET;
  addr_src.sin_port        = htons(0);    // local port
  addr_src.sin_addr.s_addr = htonl(INADDR_ANY); //<arpa/inet.h>
  // bind to local address
  if(-1 == bind(sock, (struct sockaddr *) &addr_src, addr_len)){
    printf("binding fails, force to exit/n");
    exit(1);
  }
  
  memset(&addr_dst, 0, addr_len);
  addr_dst.sin_family  = AF_INET;
  addr_dst.sin_port    = htons(port);    // server port
  struct hostent *host = gethostbyname(servname); //<netdb.h>
  if(host == NULL){
    printf("host name wrong, force to exit\n");
    exit (1);
  }
  memcpy (&(addr_dst.sin_addr.s_addr), host->h_addr_list[0], 4);
  printf("Connecting to NTP_SERVER: %s ip: %s  port: %d\n",
	 servname, inet_ntoa(addr_dst.sin_addr), port);
  if(-1 == connect(sock, (struct sockaddr *) &addr_dst, addr_len)){
    printf("connection fails, force to exit!\n");
    exit (1);
  }
  return sock;
}

// construct and send ntp package
void send_packet(int fd){
  unsigned int data[12];
  int ret;
  long long t_now;

  if(sizeof(data) != 48){
    printf( "data size error!/n" );
    exit(1);
  }
  memset(( char *)data, 0,  sizeof (data));
  data[0] = htonl((LI << 30) | (VN << 27) | (MODE << 24) 
		  | (STRATUM << 16) | (POLL << 8) | (PREC & 0xff));
  data[1] = htonl(1 << 16);
  data[2] = htonl(1 << 16);

#ifdef CLOCK_REAL     // test real clock
  struct timeval now;
  gettimeofday(&now, NULL); // get timestamp =====> change to TSC!
  data[10] = htonl(now.tv_sec + JAN_1970);
  data[11] = htonl(NTPFRAC(now.tv_usec));
#else                 // raw clock
  t_now = gettime();
  //printf("new: %lld, %lld\n", t_now / MILLION, t_now % MILLION);
  //printf("old: %lld, %lld\n", (long long) now.tv_sec, (long long) now.tv_usec);
  data[10] = htonl(t_now / MILLION + JAN_1970);
  data[11] = htonl(NTPFRAC(t_now % MILLION));  
#endif
  
  ret = send(fd, data, 48, 0);
  
#ifdef DEBUG
  printf("send packet to ntp server, ret: %d\n", ret);
#endif
}

//Acquire and Analyze NTP packet
bool get_server_time(int sock, Response *resp)
{
  int ret;
  unsigned int data[12];    
  NtpTime oritime, rectime, tratime; // destime;
  struct timeval offtime, dlytime;
  long long t_now;

  bzero(data, sizeof (data));
  
  ret = recvfrom(sock, data, sizeof (data), 0, NULL, 0);
  
#ifdef CLOCK_REAL
  struct timeval now;
  gettimeofday(&now, NULL);
#else
  t_now = gettime();
  //printf("new: %lld, %lld\n", t_now / MILLION + JAN_1970, t_now % MILLION);
  //printf("old: %lld, %lld\n", (long long) now.tv_sec + JAN_1970, (long long) now.tv_usec);
#endif
  
  if(ret < 0){
    if(ret == EWOULDBLOCK || ret== EAGAIN){ // timeout
      printf("recvfrom timeout\n");
      return false;
    } else { // other error
      printf( "recvfrom was failed! %s\n", strerror(errno));
      return false;
      //exit(1);
    }
  } else if(ret == 0){
    printf( "recvfrom receive 0!\n" );
    return false ;
  }
  //destime.integer  = now.tv_sec + JAN_1970;
  //destime.fraction = NTPFRAC(now.tv_usec);
  //destime.integer  = t_now / MILLION + JAN_1970;
  //destime.fraction = NTPFRAC(t_now % MILLION);  

  oritime.integer  = DATA(6);
  oritime.fraction = DATA(7);
  rectime.integer  = DATA(8);
  rectime.fraction = DATA(9);
  tratime.integer  = DATA(10);
  tratime.fraction = DATA(11);

  resp->stratum   = (DATA(0) & 0x00FF0000) >> 16;
  resp->precision = INT8( (int) (DATA(0) & 0x000000FF) );
  
  //resp->rootDelay = 1000000 * (DATA(1) / (double) 0x10000);
  //resp->rootDisp  = 1000000 * (DATA(2) / (double) 0x10000);
  resp->rootDelay = 0;
  resp->rootDisp  = 0;  

  //Originate Timestamp     T1    client transmit timestamp
  //Receive Timestamp       T2    server recevie timestamp
  //Transmit Timestamp      T3    server transmit timestamp
  //Destination Timestamp   T4    client receive timestamp

  long long t1 = TTLUSEC(MKSEC(oritime), MKUSEC(oritime));
  long long t2 = TTLUSEC(MKSEC(rectime), MKUSEC(rectime));
  long long t3 = TTLUSEC(MKSEC(tratime), MKUSEC(tratime));
#ifdef CLOCK_REAL
  long long t4 = TTLUSEC(now.tv_sec, now.tv_usec);
#else
  long long t4 = t_now;
#endif

  //rtt = (T2 - T1) + (T4 - T3); offset = [(T2 - T1) + (T3 - T4)] / 2;
  resp->delay  = (t2 - t1) + (t4 - t3);
  resp->offset = ((t2 - t1) + (t3 - t4)) / 2;

#ifdef DEBUG
  printf("Server response:\n");
  printf("stratum: %d, rootDelay: %lld, rootDisp: %lld\n", resp->stratum, resp->rootDelay, resp->rootDisp);
  printf("delay: %lld, offset: %lld\n\n", resp->delay, resp->offset);
#endif

  double server_prec = LOG2D(resp->precision)*1000000;
  double local_prec = LOG2D(PREC)*1000000;
  double drift = DRIFT*(t4 - t1);
  
  resp->disp = (long long) server_prec  + (long long) local_prec + (long long) drift;
  
  return true;
}

long long gettime(){
  struct timespec ts_raw;
  long long t_raw;
  clock_gettime(CLOCK_MONOTONIC_RAW, &ts_raw);
  t_raw = TTLUSEC(ts_raw.tv_sec, ts_raw.tv_nsec/1000);
  return (long long) (local_clock.ratio*(t_raw - local_clock.base_x)) + local_clock.base_y;
}
