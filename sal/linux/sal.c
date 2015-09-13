
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

/*#define CASE(sym, key) \
  case SDLK_##sym: \
	inputHeld &= ~(SAL_INPUT_##key); \
	inputHeld |= type << SAL_INPUT_INDEX_##key; \
	break
*/

static u32 inputHeld = 0;


static u32 sal_Input(int held)
{
	/*SDL_Event event;
	int i=0;
	u32 timer=0;

	if (!SDL_PollEvent(&event)) {
		if (held)
			return inputHeld;
		return 0;
	}

	Uint8 type = (event.key.state == SDL_PRESSED);
	switch(event.key.keysym.sym) {
		CASE(TAB, A);
		CASE(LCTRL, B);
		CASE(MENU, X);
		CASE(BACKSPACE, Y);
		CASE(PLUS, L);
		CASE(MINUS, R);
		CASE(RETURN, START);
		CASE(SPACE, SELECT);
		CASE(UP, UP);
		CASE(DOWN, DOWN);
		CASE(LEFT, LEFT);
		CASE(RIGHT, RIGHT);
		CASE(ESCAPE, MENU);
		default: break;
	}

	mInputRepeat = inputHeld;
	*/
	
	int i=0;
	u32 inputHeld=0;
	u32 timer=0;

	if ( isKeyPressed(KEY_NSPIRE_CTRL) )
		inputHeld|=SAL_INPUT_A;
	if ( isKeyPressed(KEY_NSPIRE_SHIFT) )
		inputHeld|=SAL_INPUT_B;
	if ( isKeyPressed(KEY_NSPIRE_VAR) )
		inputHeld|=SAL_INPUT_X;
	if ( isKeyPressed(KEY_NSPIRE_DEL) ) 
		inputHeld|=SAL_INPUT_Y;
		
	if ( isKeyPressed(KEY_NSPIRE_TAB) )
		inputHeld|=SAL_INPUT_L;
	if ( isKeyPressed(KEY_NSPIRE_MENU) ) 
		inputHeld|=SAL_INPUT_R;
		
	if ( isKeyPressed(KEY_NSPIRE_ENTER) ) 
		inputHeld|=SAL_INPUT_START;
	if (isKeyPressed(KEY_NSPIRE_MINUS) ) 
		inputHeld|=SAL_INPUT_SELECT;
		
	if ( isKeyPressed(KEY_NSPIRE_UP) || isKeyPressed(KEY_NSPIRE_8))
		inputHeld|=SAL_INPUT_UP;
	else if ( isKeyPressed(KEY_NSPIRE_DOWN) || isKeyPressed(KEY_NSPIRE_5))
		inputHeld|=SAL_INPUT_DOWN;
	if ( isKeyPressed(KEY_NSPIRE_LEFT)  || isKeyPressed(KEY_NSPIRE_4))
		inputHeld|=SAL_INPUT_LEFT;
	else if ( isKeyPressed(KEY_NSPIRE_RIGHT) || isKeyPressed(KEY_NSPIRE_6) )
		inputHeld|=SAL_INPUT_RIGHT;
		
	if (isKeyPressed(KEY_NSPIRE_7))
	{
		inputHeld|=SAL_INPUT_UP;
		inputHeld|=SAL_INPUT_LEFT;
	}	
	else if (isKeyPressed(KEY_NSPIRE_9) )
	{
		inputHeld|=SAL_INPUT_UP;
		inputHeld|=SAL_INPUT_RIGHT;
	}	
	
	if (isKeyPressed(KEY_NSPIRE_1))
	{
		inputHeld|=SAL_INPUT_DOWN;
		inputHeld|=SAL_INPUT_LEFT;
	}	
	else if ( isKeyPressed(KEY_NSPIRE_3) )
	{
		inputHeld|=SAL_INPUT_DOWN;
		inputHeld|=SAL_INPUT_RIGHT;
	}	

	if ( isKeyPressed(KEY_NSPIRE_ESC) )
		inputHeld|=SAL_INPUT_MENU;

	// Process key repeats
	timer=sal_TimerRead();
	for (i=0;i<19;i++)
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

	/*if(mInputIgnore)
	{
		//A request to ignore all key presses until all keys have been released has been made
		//check for release and clear flag, otherwise clear inputHeld and mInputRepeat
		if (inputHeld == 0)
		{
			mInputIgnore=0;
		}
		inputHeld=0;
		mInputRepeat=0;
	}*/

	return inputHeld;
}

static int key_repeat_enabled = 1;

u32 sal_InputPollRepeat()
{
	/*if (!key_repeat_enabled) {
		SDL_EnableKeyRepeat(SDL_DEFAULT_REPEAT_DELAY, SDL_DEFAULT_REPEAT_INTERVAL);
		key_repeat_enabled = 1;
	}*/
	return sal_Input(0);
}

u32 sal_InputPoll()
{
	/*if (key_repeat_enabled) {
		SDL_EnableKeyRepeat(0, 0);
		key_repeat_enabled = 0;
	}*/
	return sal_Input(1);
}

const char* sal_DirectoryGetTemp(void)
{
	char d[] = "/exammode/usr/Press-to-Test/";

	struct stat s = {0};

	if (!stat(d, &s))
	{
		return "/exammode/usr/Press-to-Test/";
	}
	else
	{
		return "/documents/ndless/";
	}
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
	/*if( SDL_Init( SDL_INIT_VIDEO ) == -1 )
	{
		return SAL_ERROR;
	}*/
	sal_TimerInit(60);

	memset(mInputRepeatTimer,0,sizeof(mInputRepeatTimer));

	/*SDL_EnableKeyRepeat(SDL_DEFAULT_REPEAT_DELAY, SDL_DEFAULT_REPEAT_INTERVAL);*/

	return SAL_OK;
}

u32 sal_VideoInit(u32 bpp)
{
	initBuffering();
	clearBufferB();
	updateScreen();
	mBpp = 16;
	/*SDL_ShowCursor(0);
	
	mBpp=bpp;

	//Set up the screen
	mScreen = SDL_SetVideoMode( SAL_SCREEN_WIDTH, SAL_SCREEN_HEIGHT, 16, SDL_SWSURFACE);

    	//If there was an error in setting up the screen
    	if( mScreen == NULL )
    	{
		sal_LastErrorSet("SDL_SetVideoMode failed");        	
		return SAL_ERROR;
    	}

    	// lock surface if needed 
	if (SDL_MUSTLOCK(mScreen)) 
	{ 
		if (SDL_LockSurface(mScreen) < 0) 
		{ 
			sal_LastErrorSet("unable to lock surface"); 
			return SAL_ERROR;
		} 
	}*/
   
	return SAL_OK;
}

u32 sal_VideoGetWidth()
{
	/*printf("mScreen->w %d\n", mScreen->w);
	return mScreen->w;*/
	return 320;
}

u32 sal_VideoGetHeight()
{
	/*printf("mScreen->h %d\", mScreen->h);
	return mScreen->h;*/
	return 240;
}

u32 sal_VideoGetPitch()
{
	/*printf("mScreen->pitch %d\n", mScreen->pitch);
	return mScreen->pitch;*/
	/*return 320;*/
	return 320*2;
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
	/*if (SDL_MUSTLOCK(mScreen)) {
		SDL_UnlockSurface(mScreen); 
		SDL_Flip(mScreen);
		SDL_LockSurface(mScreen);
	} else
		SDL_Flip(mScreen);*/
}

void *sal_VideoGetBuffer()
{
	/*return (void*)mScreen->pixels;*/
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
	/*SDL_Quit();*/
}



