#include <sal.h>
#include <sys/time.h>

static u32 mFrameTime;

//static u32 tv_sec = 0, tv_usec = 0;

u32 sal_TimerRead()
{
	struct timeval tval; // timing
  
  	gettimeofday(&tval, 0);
  	//tval.tv_usec
  	//tval.tv_sec
	return ((tval.tv_sec * 1000000) + tval.tv_usec) / mFrameTime;

	/*
	if (tval.tv_usec < tv_usec)
		tv_sec += 60000000 >> 20;
	tv_usec = tval.tv_usec;
	*/

//	return tv_sec + ((tv_usec * 60) >> 20) + 4;
//	return 4 + (tval.tv_sec * 60000000 + tval.tv_usec * 60) >> 20;
}

s32 sal_TimerInit(s32 frametime)
{
	mFrameTime = frametime;
   	return SAL_OK;
}

void sal_TimerClose(void) 
{
}
