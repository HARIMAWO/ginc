#pragma once
#pragma ginc_lib "libginc_std.so"

typedef long time_t;

extern long ginc_time(void* tloc);
extern long ginc_clock(void);
extern int ginc_sleep(int seconds);
extern int ginc_usleep(int usec);

#define time ginc_time
#define clock ginc_clock
#define sleep ginc_sleep
#define usleep ginc_usleep
