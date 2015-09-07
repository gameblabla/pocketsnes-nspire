
#include <stdio.h>
#include <dirent.h>
#include "n2DLib.h"
#include <sys/time.h>
#include "sal.h"
#include "menu.h"

#define PALETTE_BUFFER_LENGTH	256*2*4

/*static SDL_Surface *mScreen = NULL;*/
static u32 mSoundThreadFlag=0;
static u32 mSoundLastCpuSpeed=0;
static u32 mPaletteBuffer[PALETTE_BUFFER_LENGTH];
static u32 *mPaletteCurr=(u32*)&mPaletteBuffer[0];
static u32 *mPaletteLast=(u32*)&mPaletteBuffer[0];
static u32 *mPaletteEnd=(u32*)&mPaletteBuffer[PALETTE_BUFFER_LENGTH];
static u32 mInputFirst=0;

s32 mCpuSpeedLookup[1]={0};

#include <sal_common.h>


static u32 inputHeld = 0;


static u32 sal_Input(int held)
{
	int i=0;
	u32 inputHeld=0;
	u32 timer=0;
	int key=0;
	
	GetKey(&key)

	if (key == KEY_CTRL_CTRL)
		inputHeld|=SAL_INPUT_A;
	if (key == KEY_CTRL_SHIFT)
		inputHeld|=SAL_INPUT_B;
	if (key == KEY_CTRL_VAR)
		inputHeld|=SAL_INPUT_X;
	if (key == KEY_CTRL_DEL) 
		inputHeld|=SAL_INPUT_Y;
		
	if (key == KEY_CTRL_TAB)
		inputHeld|=SAL_INPUT_L;
	if (key == KEY_CTRL_MENU) 
		inputHeld|=SAL_INPUT_R;
		
	if (key == KEY_CTRL_ENTER) 
		inputHeld|=SAL_INPUT_START;
	if (key == KEY_CTRL_MINUS) 
		inputHeld|=SAL_INPUT_SELECT;
		
	if (key == KEY_CTRL_UP)
		inputHeld|=SAL_INPUT_UP;
	if (key == KEY_CTRL_DOWN)
		inputHeld|=SAL_INPUT_DOWN;
	if (key == KEY_CTRL_LEFT)
		inputHeld|=SAL_INPUT_LEFT;
	if (key == KEY_CTRL_RIGHT)
		inputHeld|=SAL_INPUT_RIGHT;
		
	if (key == KEY_CTRL_EXIT)
		inputHeld|=SAL_INPUT_MENU;

	// Process key repeats
	timer=sal_TimerRead();
	for (i=0;i<32;i++)
	{
		if (inputHeld&(1<<i)) 
		{
			if(mInputFirst&(1<<i))
			{
				if (mInputRepeatTimer[i]<timer)
				{
					mInputRepeat|=1<<i;
					mInputRepeatTimer[i]=timer+10;
				}
				else
				{
					mInputRepeat&=~(1<<i);
				}
			}
			else
			{
				//First press of button
				//set timer to expire later than usual
				mInputFirst|=(1<<i);
				mInputRepeat|=1<<i;
				mInputRepeatTimer[i]=timer+50;
			}
		}
		else			
		{
			mInputRepeatTimer[i]=timer-10;
			mInputRepeat&=~(1<<i);
			mInputFirst&=~(1<<i);
		}
		
	}

	return inputHeld;
}

static int key_repeat_enabled = 1;

u32 sal_InputPollRepeat()
{
	return sal_Input(0);
}

u32 sal_InputPoll()
{
	return sal_Input(1);
}

const char* sal_DirectoryGetTemp(void)
{
	return "/tmp";
}

void sal_CpuSpeedSet(u32 mhz)
{

}

u32 sal_CpuSpeedNext(u32 currSpeed)
{
	u32 newSpeed=currSpeed+1;
	if(newSpeed > 500) newSpeed = 500;
	return newSpeed;
}

u32 sal_CpuSpeedPrevious(u32 currSpeed)
{
	u32 newSpeed=currSpeed-1;
	if(newSpeed > 500) newSpeed = 0;
	return newSpeed;
}

u32 sal_CpuSpeedNextFast(u32 currSpeed)
{
	u32 newSpeed=currSpeed+10;
	if(newSpeed > 500) newSpeed = 500;
	return newSpeed;
}

u32 sal_CpuSpeedPreviousFast(u32 currSpeed)
{
	u32 newSpeed=currSpeed-10;
	if(newSpeed > 500) newSpeed = 0;
	return newSpeed;
}

s32 sal_Init(void)
{
	sal_TimerInit(60);
	memset(mInputRepeatTimer,0,sizeof(mInputRepeatTimer));
	return SAL_OK;
}

u32 sal_VideoInit(u32 bpp)
{
	initBuffering();
	mBpp = 16;

	return SAL_OK;
}

u32 sal_VideoGetWidth()
{
	return 384;
}

u32 sal_VideoGetHeight()
{
	return 240;
}

u32 sal_VideoGetPitch()
{
	return 384*2;
}

void sal_VideoEnterGame(u32 fullscreenOption, u32 pal, u32 refreshRate)
{
}

void sal_VideoSetPAL(u32 fullscreenOption, u32 pal)
{
	if (fullscreenOption == 3) /* hardware scaling */
	{
		sal_VideoEnterGame(fullscreenOption, pal, mRefreshRate);
	}
}

void sal_VideoExitGame()
{
}

void sal_VideoBitmapDim(u16* img, u32 pixelCount)
{
	u32 i;
	for (i = 0; i < pixelCount; i += 2)
		*(u32 *) &img[i] = (*(u32 *) &img[i] & 0xF7DEF7DE) >> 1;
	if (pixelCount & 1)
		img[i - 1] = (img[i - 1] & 0xF7DE) >> 1;
}

void sal_VideoFlip(s32 vsync)
{
	 updateScreen();
}

void *sal_VideoGetBuffer()
{
	return (void*)BUFF_BASE_ADDRESS;
}

void sal_VideoPaletteSync() 
{ 	
	
} 

void sal_VideoPaletteSet(u32 index, u32 color)
{
	*mPaletteCurr++=index;
	*mPaletteCurr++=color;
	if(mPaletteCurr>mPaletteEnd) mPaletteCurr=&mPaletteBuffer[0];
}

void sal_Reset(void)
{
	deinitBuffering();
}



