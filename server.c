/*
 * Local NTP (LNTP)
 * Author: Zhou Fang (zhoufang@ucsd.edu)
 * Reference: 
 * 1. NTPv4: https://www.eecis.udel.edu/~mills/database/reports/ntp4/ntp4.pdf
 * 2. Use some source code from: http://blog.csdn.net/rich_baba/article/details/6052863
 */

#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <netdb.h>
#include <sys/types.h> 
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#define BUFSIZE 1024

#define PORT 8000

#define CLOCK_REAL
#define PREC -10       // local clock precision ~1ms

// ntp package send head
#define  LI 0          // protocol head elements
#define  VN 4          // version
#define  MODE 4        // mode: server response
#define  STRATUM 0
#define  POLL 4        // maximal interval between requests

#define  JAN_1970   0x83aa7e80      //3600s*24h*(365days*70years+17days)
#define  NTPFRAC(x) (4294 * (x) + ((1981 * (x)) >> 11)) 
#define  DATA(i) ntohl((( unsigned   int  *)data)[i])

#define  USEC(x) (((x) >> 12) - 759 * ((((x) >> 10) + 32768) >> 16))
#define  MKSEC(ntpt)   ((ntpt).integer - JAN_1970)
#define  MKUSEC(ntpt)  (USEC((ntpt).fraction))
#define  TTLUSEC(sec, usec) ((long long)(sec)*1000000 + (usec))
#define  GETSEC(us)    ((us)/1000000) 
#define  GETUSEC(us)   ((us)%1000000)

typedef struct{
  unsigned   int  integer;
  unsigned   int  fraction;
} NtpTime;

/*
 * error - wrapper for perror
 */
void error(char *msg) {
  perror(msg);
  exit(1);
}

int main(int argc, char **argv) {
  int sockfd; /* socket */
  int portno = PORT; /* port to listen on */
  int clientlen; /* byte size of client's address */
  struct sockaddr_in serveraddr; /* server's addr */
  struct sockaddr_in clientaddr; /* client addr */
  struct hostent *hostp; /* client host info */
  unsigned int data[12];
  char *hostaddrp; /* dotted decimal host addr string */
  int optval; /* flag value for setsockopt */
  int n; /* message byte size */
  
  struct timeval ts_rcv, ts_xmt;

  /* 
   * socket: create the parent socket 
   */
  sockfd = socket(AF_INET, SOCK_DGRAM, 0);
  if (sockfd < 0) 
    error("ERROR opening socket");

  /* setsockopt: Handy debugging trick that lets 
   * us rerun the server immediately after we kill it; 
   * otherwise we have to wait about 20 secs. 
   * Eliminates "ERROR on binding: Address already in use" error. 
   */
  optval = 1;
  setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, 
	     (const void *)&optval , sizeof(int));

  /*
   * build the server's Internet address
   */
  bzero((char *) &serveraddr, sizeof(serveraddr));
  serveraddr.sin_family = AF_INET;
  serveraddr.sin_addr.s_addr = htonl(INADDR_ANY);
  serveraddr.sin_port = htons((unsigned short)portno);

  /* 
   * bind: associate the parent socket with a port 
   */
  if (bind(sockfd, (struct sockaddr *) &serveraddr, 
	   sizeof(serveraddr)) < 0) 
    error("ERROR on binding");

  /* 
   * main loop: wait for a datagram, then echo it
   */
  clientlen = sizeof(clientaddr);
  while (1) {
    /*
     * recvfrom: receive a UDP datagram from a client
     */
    bzero(data, BUFSIZE);
    n = recvfrom(sockfd, data, BUFSIZE, 0,
		 (struct sockaddr *) &clientaddr, &clientlen);
    
    // get rev timestamp
    gettimeofday(&ts_rcv, NULL);

    if (n < 0) error("ERROR in recvfrom");
    if (n != 48) error("corrupted NTP packet");

    /* 
     * gethostbyaddr: determine who sent the datagram
     */
    hostp = gethostbyaddr((const char *)&clientaddr.sin_addr.s_addr, 
			  sizeof(clientaddr.sin_addr.s_addr), AF_INET);
    if (hostp == NULL)
      error("ERROR on gethostbyaddr");
    hostaddrp = inet_ntoa(clientaddr.sin_addr);
    if (hostaddrp == NULL)
      error("ERROR on inet_ntoa\n");
    
    printf("server received datagram from %s (%s) len = %d\n", 
	   hostp->h_name, hostaddrp, n);

    data[0] = htonl((LI << 30) | (VN << 27) | (MODE << 24) 
		  | (STRATUM << 16) | (POLL << 8) | (PREC & 0xff));

    data[6] = data[10];
    data[7] = data[11];

    data[8] = htonl(ts_rcv.tv_sec + JAN_1970);
    data[9] = htonl(NTPFRAC(ts_rcv.tv_usec));

    gettimeofday(&ts_xmt, NULL);
    data[10] = htonl(ts_xmt.tv_sec + JAN_1970);
    data[11] = htonl(NTPFRAC(ts_xmt.tv_usec));
    /* 
     * sendto: echo the input back to the client 
     */
    n = sendto(sockfd, data, 48, 0, 
	       (struct sockaddr *) &clientaddr, clientlen);
    if (n < 0) 
      error("ERROR in sendto");
  }
}
