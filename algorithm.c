/*
 * Local NTP (LNTP)
 * Author: Zhou Fang (zhoufang@ucsd.edu)
 * Reference: 
 * 1. NTPv4: https://www.eecis.udel.edu/~mills/database/reports/ntp4/ntp4.pdf
 * 2. Use some source code from: http://blog.csdn.net/rich_baba/article/details/6052863
 */

#include "ntpclient.h"

void filter(Response[], Interval*, const int);
int select_clock(const Interval*, const int);

int i_sample = 0;

// apply NTPv4 algorithms
void ntp_process(){
  struct timespec ts_raw;
  long long t_raw;
  
  int i;
  Response resp;

#ifdef DEBUG
  printf("\nTotal is: %d\n", total);
  for(i = 0; i < total; i++){
    resp = resp_list[i];
    printf("%d %d %lld %lld %lld\n", i, resp.stratum, resp.delay, resp.offset, resp.disp);
    printf("%d %d %lld %lld\n", i, resp.precision, resp.rootDelay, resp.rootDisp);
  }
  printf("\n");
#endif
  
  Interval interv_list[total];
  filter(resp_list, interv_list, total);

  // verifiy intervals
  /*
  for(i = 0; i < total; i++){
    printf("%d %lld %lld %lld %lld\n", interv_list[i].stratum, interv_list[i].offset, interv_list[i].low, interv_list[i].up, interv_list[i].rootDist);
  }
  */

  int ret = select_clock(interv_list, total);
  if(ret == -1) {
    printf("sync fails, no valid bound\n");
    reset_ratio();
    return;
  }

  // ==================== generate survivors ================
  int n_survivor = 0;
  Interval *interv;
  for(i = 0; i < total; i++){
    interv = interv_list+i;
    if(interv->offset > local_clock.low && interv->offset < local_clock.up) n_survivor++;
  }
  
  Survivor surv_list[n_survivor];
  long long weight;
  n_survivor = 0;
  for(i = 0; i < total; i++){
    interv = interv_list+i;
    if(interv->offset > local_clock.low && interv->offset < local_clock.up){
      weight = interv->stratum * MAXDIST + interv->rootDist;
      surv_list[n_survivor].offset   = interv->offset;
      surv_list[n_survivor].rootDist = interv->rootDist;
      surv_list[n_survivor].stratum  = interv->stratum;
      surv_list[n_survivor].weight   = weight;
      n_survivor++;
    }
  }
  
  if(n_survivor < N_MIN_SURVIVOR){
    printf("Sync fails, no enough survivor\n");
    reset_ratio();
    return;  // quit NTP
  }

  // =============== clustering algorithm ===============
  // ==== sort survivors according to weight ====
  Survivor surv_tem;
  long long min_d;
  int i_min;
  int j;
  for(i = 0; i < n_survivor; i++){
    for(j = i; j < n_survivor; j++){ // find min
      if(j == i) { // init
	min_d = surv_list[j].weight;
	i_min = j;
      } else if(min_d > surv_list[j].weight) {
	min_d = surv_list[j].weight;
	i_min = j;
      }
    }
    surv_tem = surv_list[i];
    surv_list[i] = surv_list[i_min];
    surv_list[i_min] = surv_tem;
  }
  
#ifdef DEBUG
  printf("\n Survivors:\n");
  for(i = 0; i < n_survivor; i++){
    printf("%d %lld %lld %lld\n", surv_list[i].stratum, surv_list[i].offset, surv_list[i].weight, surv_list[i].rootDist);
  }
#endif
  
  long long maxJitter = 0, minJitter = 0, phi, s_i, s_j;
  int maxIndex = 0, minIndex = 0;
  while(true){
    for(i = 0; i < n_survivor; i++){
      phi = 0;
      s_i = surv_list[i].offset;
      for(j = 0; j < n_survivor; j++){
	s_j = surv_list[j].offset;
	if(i != j){
	  phi += (s_i - s_j)*(s_i - s_j);
	}
      }
      if(i == 0){
	maxJitter = phi; minJitter = phi;
      } else {
	if(phi > maxJitter) {
	  maxJitter = phi; maxIndex = i;
	} else if(phi < minJitter) {
	  minJitter = phi; minIndex = i;
	}
      }
    }
    if(maxJitter < minJitter || n_survivor <= N_MIN_SURVIVOR) break;
    for(i = maxIndex; i < n_survivor-1; i++){ // remove survivor
      surv_list[maxIndex] = surv_list[maxIndex+1]; 
    }
    n_survivor--; 
  }

#ifdef DEBUG
  printf("\n Rest Survivors:\n");
  for(i = 0; i < n_survivor; i++){
    printf("%d %lld %lld %lld\n", surv_list[i].stratum, surv_list[i].offset, surv_list[i].weight, surv_list[i].rootDist);
  }
  struct timeval tv_real;
  gettimeofday(&tv_real, NULL); // real time clock
  long long t_real = TTLUSEC(tv_real.tv_sec, tv_real.tv_usec);
  long long t_sync = gettime();
  printf("Compare clock: real %lld, sync %lld, diff %lld\n\n", t_real, t_sync, t_real-t_sync);
#endif

  // =============== combination ==============
  long long offset0 = surv_list[0].offset;
  Survivor *surv;
  double x = 0, y = 0, w = 0, z = 0;
  for(i = 0; i < n_survivor; i++){
    surv = &surv_list[i];
    x = surv->rootDist;
    y += 1/x;
    z += surv->offset/x;
    w += (surv->offset - offset0)*(surv->offset - offset0);    
  }
  local_clock.offset = (long long) (z/y);
  local_clock.jitter = (long long) sqrt(w/y);
  local_clock.index ++; 
  
  clock_gettime(CLOCK_MONOTONIC_RAW, &ts_raw);
  t_raw = TTLUSEC(ts_raw.tv_sec, ts_raw.tv_nsec/1000);
  local_clock.base_y = local_clock.ratio*(t_raw - local_clock.base_x) + local_clock.base_y;
  local_clock.base_x = t_raw;
  local_clock.ratio = 1 + (double) local_clock.offset / (double) (DEF_PSEC * MILLION) * RATIO_SCALE;  

  // write result via mmap
  mapped[1] = local_clock.offset;
  mapped[2] = local_clock.base_x;
  mapped[3] = local_clock.base_y;
  mapped[4] = local_clock.low;
  mapped[5] = local_clock.up;
  mapped[6] = local_clock.period;
  mapped[7] = local_clock.jitter;
  // "index" is the last one to change
  // if client detects that index is changed, all values have been updated
  // otherwise old values are used by client
  mapped[0] = local_clock.index;

  printf("Rseult: ratio = %lf, offset = %lld, low = %lld, up = %lld, jitter = %lld\n\n", \
	 local_clock.ratio, local_clock.offset, local_clock.low, local_clock.up, local_clock.jitter);

#ifdef RECORD
  if(i_sample == NUM_SAMPLE) {
    printf("\nData Colection finished\n\n");
    exit(0);
  }
  i_sample ++;
  fprintf(fp_result, "%lf %lld %lld %lld\n", local_clock.ratio, local_clock.offset, local_clock.low, local_clock.up);  
#endif

  return;
}

void filter(Response resp_list[], Interval *interv_list, const int total) {  
  // ========== sort response according to delay from min to max =========
  int i, j, i_min;
  long long min_d;
  Response resp_tem;
  for(i = 0; i < total; i++){
    for(j = i; j < total; j++){ // find min
      if(j == i) { // init
	min_d = resp_list[j].delay;
	i_min = j;
      } else if(min_d > resp_list[j].delay) {
	min_d = resp_list[j].delay;
	i_min = j;
      }
    }
    // === exchange i_min and i ===
    resp_tem = resp_list[i];
    resp_list[i] = resp_list[i_min];
    resp_list[i_min] = resp_tem;
  }
  
  // ======== filtering ========
  long long peer_disp = 0;
  long long sum = 0;
  long long diff;
  for(i = 0; i < total; i++) {
    // calculate peer dispersion
    peer_disp += (resp_list[i].disp >> (i+1));
    // calculate offset jitter
    diff = resp_list[i].offset - resp_list[0].offset;
    sum += SQUARE(diff);
  }
  long long jitter = (long long) sqrt(sum/total);
  
#ifdef DEBUG
  printf("peer_disp: %lld\n", peer_disp);
  printf("jitter: %lld\n", jitter);
#endif

  // calculate root distance
  long long root_dist;
  Interval interval;
  for(i = 0; i < total; i++) {
    root_dist = MAX(MINDISP, resp_list[i].rootDelay + resp_list[i].delay)/2 + \
      peer_disp + jitter;
    interval.offset = resp_list[i].offset;
    interval.stratum = resp_list[i].stratum;
    interval.rootDist = root_dist;
    interval.up = resp_list[i].offset + root_dist;
    interval.low = resp_list[i].offset - root_dist;
    *(interv_list + i) = interval;
  }
}

int select_clock(const Interval *interv_list, const int total){
  const int total_point = total*3;
  Point point_list[total_point];
  int i, j;
  for(i = 0; i < total; i++){
    j = i*3;
    point_list[j].type = 0; // low
    point_list[j].offset = (*(interv_list + i)).low;
    point_list[j+1].type = 1; // mean
    point_list[j+1].offset = (*(interv_list + i)).offset;
    point_list[j+2].type = 2; // high
    point_list[j+2].offset = (*(interv_list + i)).up;
  }
  // ==== sort point according to offset ====
  Point point_tem;
  long long min_d;
  int i_min;
  for(i = 0; i < total_point; i++){
    for(j = i; j < total_point; j++){ // find min
      if(j == i) { // init
	min_d = point_list[j].offset;
	i_min = j;
      } else if(min_d > point_list[j].offset) {
	min_d = point_list[j].offset;
	i_min = j;
      }
    }
    point_tem = point_list[i];
    point_list[i] = point_list[i_min];
    point_list[i_min] = point_tem;
  }
  
#ifdef DEBUG
  printf("\n");
  for(i = 0; i < 3*total; i++){
    printf("point: %d, %lld\n", point_list[i].type, point_list[i].offset);
  }
#endif
  
  int m = total, f = 0, d = 0, c = 0, l = 0, u = 0;
  Point *p;
  while(true){
    d = 0; c = 0;
    for(i = 0; i < total_point; i++){
      p = &point_list[i];
      switch(p->type){
      case 0: c++; break;
      case 1: d++; break;
      case 2: c--; break;
      default: printf("error: invalid point\n"); exit(1);
      }
      if(c >= m - f){
	l = p->offset; break;
      }
    }
    c = 0;
    for(i = total_point - 1; i >= 0; i--){
      p = &point_list[i];
      switch(p->type){
      case 2: c++; break;
      case 1: d++; break;
      case 0: c--; break;
      default: printf("error: invalid point\n"); exit(1);
      }
      if(c >= m - f){
	u = p->offset; break;
      }
    }
    if(d <= f && l < u) break;
    else {
      f++;
      if(f >= m/2) return -1; // selection failure
    }
  }

  local_clock.low = l;
  local_clock.up = u;
    
  return 1;
}

void reset_ratio(){
  struct timespec ts_raw;
  long long t_raw;
  // reset clock model
  clock_gettime(CLOCK_MONOTONIC_RAW, &ts_raw);
  t_raw = TTLUSEC(ts_raw.tv_sec, ts_raw.tv_nsec/1000);
  local_clock.base_y = local_clock.ratio*(t_raw - local_clock.base_x) + local_clock.base_y;
  local_clock.base_x = t_raw;
  local_clock.ratio = 1;
}
