#include <errno.h>

#include "sal.h"
#include "menu.h"
#include "snapshot.h"
#include "snes9x.h"
#include "gfx.h"
#include "memmap.h"
#include "soundux.h"

#define MAX_DISPLAY_CHARS			40

#define ROM_SELECTOR_SAVE_DEFAULT_DIR		0
#define ROM_SELECTOR_MAIN_MENU			1
#define ROM_SELECTOR_DEFAULT_FOCUS		2
#define ROM_SELECTOR_ROM_START			3

static u16 mMenuBackground[SAL_SCREEN_WIDTH * SAL_SCREEN_HEIGHT];

static s32 mMenutileXscroll=0;
static s32 mMenutileYscroll=0;
static s32 mTileCounter=0;
static s32 mQuickSavePresent=0;
static u32 mPreviewingState=0;

static s8 mMenuText[30][MAX_DISPLAY_CHARS];

static struct SAL_DIRECTORY_ENTRY *mRomList=NULL;
static s32 mRomCount;
static s8 mRomDir[SAL_MAX_PATH]={""};

static struct SAVE_STATE mSaveState[10];  // holds the filenames for the savestate and "inuse" flags
static s8 mSaveStateName[SAL_MAX_PATH]={""};       // holds the last filename to be scanned for save states
static s8 mRomName[SAL_MAX_PATH]={""};
static s8 mSystemDir[SAL_MAX_PATH];
static struct MENU_OPTIONS *mMenuOptions=NULL;
static u16 mTempFb[SNES_WIDTH*SNES_HEIGHT_EXTENDED];

									
void DefaultMenuOptions(void)
{
	mMenuOptions->frameSkip=0;   //auto
	mMenuOptions->soundEnabled = 1; 
	mMenuOptions->volume=25; 
	mMenuOptions->cpuSpeed=336; 
	mMenuOptions->country=0;
	mMenuOptions->showFps=0;
	mMenuOptions->soundRate=44100;
	mMenuOptions->stereo=1;
	mMenuOptions->fullScreen=0;
	mMenuOptions->autoSaveSram=1;
	mMenuOptions->soundSync=1;
}

void MenuPause()
{
	sal_InputWaitForPress();
	sal_InputWaitForRelease();
}

s32 MenuMessageBox(const char *message1, const char *message2,
			const char *message3, enum MENU_MESSAGE_BOX_MODE mode)
{
  s32 select=0;
  s32 subaction=-1;
  u32 keys=0;
  
  sal_InputIgnore();
  while(subaction==-1)
  {
     keys=sal_InputPollRepeat();
     if (keys & SAL_INPUT_UP) 
     {
       select=SAL_OK; // Up
     }
     if (keys & SAL_INPUT_DOWN) 
     {
       select=SAL_ERROR; // Down
     }
     if ((keys&INP_BUTTON_MENU_SELECT) || (keys&INP_BUTTON_MENU_CANCEL))
     {
        subaction=select;
     }
     PrintTitle("Message Box");
     sal_VideoPrint(8,50,message1,SAL_RGB(31,31,31));
     sal_VideoPrint(8,60,message2,SAL_RGB(31,31,31));
     sal_VideoPrint(8,70,message3,SAL_RGB(31,31,31));
     switch(mode)
     {
        case MENU_MESSAGE_BOX_MODE_YESNO: // yes no input
	       if(select==SAL_OK)
	       {
			  /*PrintBar(120-4);*/
	          sal_VideoPrint(8,120,"YES",SAL_RGB(31,31,31));
	          sal_VideoPrint(8,140,"NO",SAL_RGB(31,31,31));
	       }
	       else
	       {
			  /*PrintBar(140-4);*/
	          sal_VideoPrint(8,120,"YES",SAL_RGB(31,31,31));
	          sal_VideoPrint(8,140,"NO",SAL_RGB(31,31,31));
	       }
	       break;
	case MENU_MESSAGE_BOX_MODE_PAUSE: 
			/*PrintBar(120-4);*/
			sal_VideoPrint(8,120,"Press button to continue",SAL_RGB(31,31,31));
			break;
	case MENU_MESSAGE_BOX_MODE_MSG:
			subaction=SAL_OK;
			break;
     }
     sal_VideoFlip(1);
  }
  sal_InputIgnore();
  return(subaction);
}

void PrintTitle(const char *title)
{	
	sal_ImageDraw(mMenuBackground,SAL_SCREEN_WIDTH, SAL_SCREEN_HEIGHT,0,0);	
}

/*void PrintBar(u32 givenY)
{
	sal_HighlightBar( 262, HIGHLIGHT_BAR_HEIGHT, 0, givenY);
}*/

/*
static bool8 LoadStateTemp()
{
	char name[SAL_MAX_PATH];
	bool8 ret;
	sprintf(name, "%s%s%s", sal_DirectoryGetTemp(), SAL_DIR_SEP, ".svt.tns");
	if (!(ret = S9xUnfreezeGame(name)))
		fprintf(stderr, "Failed to read saved state at %s: %s\n", name, strerror(errno));
	return ret;
}

static void SaveStateTemp()
{
	char name[SAL_MAX_PATH];
	sprintf(name, "%s%s%s", sal_DirectoryGetTemp(), SAL_DIR_SEP, ".svt.tns");
	if (!S9xFreezeGame(name))
		fprintf(stderr, "Failed to write saved state at %s: %s\n", name, strerror(errno));
}

static void DeleteStateTemp()
{
	char name[SAL_MAX_PATH];
	sprintf(name, "%s%s%s", sal_DirectoryGetTemp(), SAL_DIR_SEP, ".svt.tns");
	sal_FileDelete(name);
}

static bool8 LoadStateFile(s8 *filename)
{
	bool8 ret;
	if (!(ret = S9xUnfreezeGame(filename)))
		fprintf(stderr, "Failed to read saved state at %s: %s\n", filename, strerror(errno));
	return ret;
}

static void SaveStateFile(s8 *filename)
{
	if (!S9xFreezeGame(filename))
		fprintf(stderr, "Failed to write saved state at %s: %s\n", filename, strerror(errno));
}
*/

static void RenderMenu(const char *menuName, s32 menuCount, s32 menuSmooth, s32 menufocus)
{
	
	s32 i=0;
	u16 color=0;
	PrintTitle(menuName);

    	for (i=0;i<menuCount;i++)
    	{
      		int x=0,y=0;

      		y=(i<<4)-(menuSmooth>>4);
			x=8;
      		y+=112 - 28;

      		if (y<=48 - 28 || y>=232 - 36) continue;
      
      		if (i==menufocus)
      		{
        		color=SAL_RGB(31,31,31);
			/*PrintBar(y-4);*/
      		}
      		else
      		{
        		color=SAL_RGB(31,31,31);
      		}

      		sal_VideoPrint(x,y,mMenuText[i],color);
    	}
}


extern "C" void S9xSaveSRAM(int showWarning);

s32 MenuRun(s8 *romName)
{
	s32 menuExit=0,menuCount=MENU_COUNT,menufocus=0,menuSmooth=0;
	s32 action=EVENT_NONE;
	s32 subaction=0;
	u32 keys=0;

	sal_CpuSpeedSet(MENU_NORMAL_CPU_SPEED);

	if(sal_StringCompare(mRomName,romName)!=0)
	{
		action=EVENT_LOAD_ROM;
		strcpy(mRomName,romName);
		return action;
	}

	sal_InputIgnore();

	while (!menuExit)
	{
		sal_VideoFlip(1);
		keys=sal_InputPollRepeat();
	}
	
  sal_InputWaitForRelease();

  return action;
}



