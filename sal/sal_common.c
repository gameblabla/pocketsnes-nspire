
/*
sal_common.c 

This file contains the code which is common to all SALs
It should be included in the sal.c of each SAL.
It just saves duplicating the same code all over the place
which is a pain in the ass especially as I'm always changing things

*/

#include <sal.h>
#include <png.h>
#include <unzip.h>
#include <zip.h>

#include "sal_common.h"

#define SAL_FRAME_BUFFER_COUNT	4

u32 mInputRepeat=0;
u32 mInputRepeatTimer[32];
u32 mBpp=16;
u32 mRefreshRate=60;

static u32 mFont8x8[]= {
0x0,0x0,0xc3663c18,0x3c2424e7,0xe724243c,0x183c66c3,0xc16f3818,0x18386fc1,0x83f61c18,0x181cf683,0xe7c3993c,0x3c99c3,0x3f7fffff,0xe7cf9f,0x3c99c3e7,0xe7c399,0x3160c080,0x40e1b,0xcbcbc37e,0x7ec3c3db,0x3c3c3c18,0x81c087e,0x8683818,0x60f0e08,0x81422418,0x18244281,0xbd5a2418,0x18245abd,0x818181ff,0xff8181,0xa1c181ff,0xff8995,0x63633e,0x3e6363,0x606060,0x606060,0x3e60603e,0x3e0303,0x3e60603e,0x3e6060,0x3e636363,0x606060,0x3e03033e,0x3e6060,0x3e03033e,0x3e6363,
0x60603e,0x606060,0x3e63633e,0x3e6363,0x3e63633e,0x3e6060,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x18181818,0x180018,0x666666,0x0,0x367f3600,0x367f36,0x3c067c18,0x183e60,0x18366600,0x62660c,0xe1c361c,0x6e337b,0x181818,0x0,0x18183870,0x703818,0x18181c0e,0xe1c18,0xff3c6600,0x663c,0x7e181800,0x1818,0x0,0x60c0c00,0x7e000000,0x0,0x0,0x181800,0x18306040,0x2060c,0x6e76663c,0x3c6666,0x18181c18,0x7e1818,0x3060663c,0x7e0c18,0x3018307e,0x3c6660,
0x363c3830,0x30307e,0x603e067e,0x3c6660,0x3e06063c,0x3c6666,0x1830607e,0xc0c0c,0x3c66663c,0x3c6666,0x7c66663c,0x1c3060,0x181800,0x1818,0x181800,0xc1818,0xc183060,0x603018,0x7e0000,0x7e00,0x30180c06,0x60c18,0x3060663c,0x180018,0x5676663c,0x7c0676,0x66663c18,0x66667e,0x3e66663e,0x3e6666,0x606663c,0x3c6606,0x6666361e,0x1e3666,0x3e06067e,0x7e0606,0x3e06067e,0x60606,0x7606067c,0x7c6666,0x7e666666,0x666666,0x1818183c,0x3c1818,0x60606060,0x3c6660,0xe1e3666,
0x66361e,0x6060606,0x7e0606,0x6b7f7763,0x636363,0x7e7e6e66,0x666676,0x6666663c,0x3c6666,0x3e66663e,0x60606,0x6666663c,0x6c366e,0x3e66663e,0x666636,0x3c06663c,0x3c6660,0x1818187e,0x181818,0x66666666,0x7c6666,0x66666666,0x183c66,0x6b636363,0x63777f,0x183c6666,0x66663c,0x3c666666,0x181818,0x1830607e,0x7e060c,0x18181878,0x781818,0x180c0602,0x406030,0x1818181e,0x1e1818,0x63361c08,0x0,0x0,0x7f0000,0xc060300,0x0,0x603c0000,0x7c667c,0x663e0606,0x3e6666,0x63c0000,
0x3c0606,0x667c6060,0x7c6666,0x663c0000,0x3c067e,0xc3e0c38,0xc0c0c,0x667c0000,0x3e607c66,0x663e0606,0x666666,0x181c0018,0x3c1818,0x18180018,0xe181818,0x36660606,0x66361e,0x1818181c,0x3c1818,0x7f370000,0x63636b,0x663e0000,0x666666,0x663c0000,0x3c6666,0x663e0000,0x63e6666,0x667c0000,0x607c6666,0x663e0000,0x60606,0x67c0000,0x3e603c,0x187e1800,0x701818,0x66660000,0x7c6666,0x66660000,0x183c66,0x63630000,0x363e6b,0x3c660000,0x663c18,0x66660000,0x3e607c66,0x307e0000,0x7e0c18,0xc181870,0x701818,0x18181818,0x18181818,0x3018180e,0xe1818,0x794f0600,0x30};

/*static s32 mAudioRateLookup[] = {
	11025, 22050, 32000, 44100, 48000,
};*/

static s8 mLastError[256]={0};
static u32 mInputHeld=0;
static u32 mInputIgnore=0;

s8 *sal_LastErrorGet()
{
	return &mLastError[0]; 
}

void sal_LastErrorSet(char *msg)
{
	strcpy(mLastError,msg);
}

/* alekmaul's scaler taken from mame4all */
void sal_VideoBitmapScale(int startx, int starty, int viswidth, int visheight, int newwidth, int newheight,int pitch, uint16_t *src, uint16_t *dst) 
{
  unsigned int W,H,ix,iy,x,y;
  x=startx<<16;
  y=starty<<16;
  W=newwidth;
  H=newheight;
  ix=(viswidth<<16)/W;
  iy=(visheight<<16)/H;

  do
  {
    u16 *buffer_mem=&src[(y>>16)*viswidth];
    W=newwidth; x=startx<<16;
    do {
      *dst++=buffer_mem[x>>16];
      x+=ix;
    } while (--W);
    dst+=pitch;
    y+=iy;
  } while (--H);
}

static void sal_VideoDrawRect16(s32 x, s32 y, s32 width, s32 height, u16 color)
{
	u16 *pixy = (u16*)sal_VideoGetBuffer();
	u16 *pixx;
	s32 h,w;
	pixy = ((u16*) ((u8*) pixy + y * sal_VideoGetPitch())) + x;
	for(h=0;h<height;h++)
	{
		pixx=pixy;		
		for(w=0; w<width; w++)
		{
			*pixx++ = color;
		}
		pixy = (u16*) ((u8*) pixy + sal_VideoGetPitch());
	}
}

void sal_VideoDrawRect(s32 x, s32 y, s32 width, s32 height, u32 color)
{
	 sal_VideoDrawRect16(x,y,width,height,(u16)color);
}

static void sal_VideoPrint16(s32 x, s32 y, const char *buffer, u16 color)
{
	s32 m,b;
	u16 *pix = (u16*)sal_VideoGetBuffer();
	s32 len=0;
	s32 maxLen=(sal_VideoGetWidth()>>3)-(x>>3);

	pix = ((u16*) ((u8*) pix + y * sal_VideoGetPitch())) + x;
	while(1) 
	{
		s8 letter = *buffer++;
		u32 *offset;

		//Check for end of string
		if (letter == 0) break;

		//Get pointer to graphics for letter
		offset = mFont8x8+((letter)<<1);
		
		//read first 32bits of char pixel mask data
		for (m=0; m<2; m++)
		{
			u32 mask = *offset++;

			//process 32bits of data in 8bit chunks
			for (b=0; b<4; b++)
			{
				if(mask&(1<<0)) pix[0] = color;
				if(mask&(1<<1)) pix[1] = color;
				if(mask&(1<<2)) pix[2] = color;
				if(mask&(1<<3)) pix[3] = color;
				if(mask&(1<<4)) pix[4] = color;
				if(mask&(1<<5)) pix[5] = color;
				if(mask&(1<<6)) pix[6] = color;
				if(mask&(1<<7)) pix[7] = color;
				pix=(u16*) ((u8*) pix + sal_VideoGetPitch()); //move to next line
				mask>>=8; //shift mask data ready for next loop
			}
		}
		//position pix pointer to start of next char
		pix = (u16*) ((u8*) pix - (sal_VideoGetPitch() << 3)) + (1 << 3);

		len++;
		if (len>=maxLen-1) break;
	}
}

void sal_VideoPrint(s32 x, s32 y, const char *buffer, u32 color)
{
	sal_VideoPrint16(x,y,buffer,(u16)color);
}

static void sal_VideoClear16(u16 color)
{
	s32 x,y,w,h,pitch;
	u16 *pix=(u16*)sal_VideoGetBuffer();

	w = sal_VideoGetWidth();
	h = sal_VideoGetHeight();
	pitch = sal_VideoGetPitch();

	for (y=0;y<h;y++)
	{
		for (x=0;x<w;x++)
		{
			*pix++ = color;
		}
		pix = (u16*) ((u8*) pix + pitch - w * sizeof(u16));
	}
}

void sal_VideoClear(u32 color)
{
	sal_VideoClear16((u16)color);
}

void sal_VideoClearAll(u32 color)
{
	u32 x;
	for (x=0;x<SAL_FRAME_BUFFER_COUNT; x++)
	{
		sal_VideoClear(color);
		sal_VideoFlip(1);
	}
}

u32 sal_InputHeld()
{
	return mInputHeld;
}

u32 sal_InputRepeat()
{
	return mInputRepeat;
}

void sal_InputWaitForRelease()
{
	int i=0;
	unsigned int keys=0;
	
	for(i=0;i<100;i++)
	{
		while(1)
		{     
			keys=sal_InputPoll();
			if(keys==0) break;
		}
	}
}

void sal_InputWaitForPress()
{
	int i=0;
	unsigned int keys=0;
	
	for(i=0;i<100;i++)
	{
		while(1)
		{     
			keys=sal_InputPoll();
			if(keys!=0) break;
		}
	}
}

void sal_InputIgnore()
{
	mInputIgnore=1;
}

u32 sal_VideoGetBpp()
{
	return mBpp;
}

s32 sal_FileGetSize(const char *filename, u32 *filesize)
{
	FILE *stream=NULL;

	stream=fopen(filename,"rb");
	if(stream)
	{
		// File exists do try to load it
		fseek(stream,0,SEEK_END);
		*filesize=ftell(stream);
		fclose(stream);
		return SAL_OK;
	}
	else
	{
		sal_LastErrorSet("Failed to open file");
		*filesize=0;
		return SAL_ERROR;
	}
}

s32 sal_FileLoad(const char *filename, u8 *buffer, u32 maxsize, u32 *filesize)
{
	FILE *stream=NULL;
	u32 size=0;

	stream=fopen(filename,"rb");
	if(stream)
	{
		// File exists do try to load it
		fseek(stream,0,SEEK_END);
		size=ftell(stream);
		if (size>maxsize) size=maxsize;
		*filesize=size;
		fseek(stream,0,SEEK_SET);
		fread(buffer, 1, size, stream);
		fclose(stream);
		return SAL_OK;
	}
	else
	{
		sal_LastErrorSet("Failed to open file");
		*filesize=0;
		return SAL_ERROR;
	}

}

s32 sal_FileSave(const char *filename, u8 *buffer, u32 bufferSize)
{
	FILE *stream=NULL;

	stream=fopen(filename,"wb");
	if(stream)
	{
		fwrite(buffer, 1, bufferSize, stream);
		fclose(stream);
		return SAL_OK;
	}
	else
	{
		return SAL_ERROR;
	}
}

s32 sal_FileExists(const char *filename)
{
	FILE *stream=NULL;

	stream=fopen(filename,"rb");
	if(stream)
	{
		// File exists
		fclose(stream);
		return SAL_TRUE;
	}
	else
	{
		return SAL_FALSE;
	}
}

s32 sal_FileDelete(const char *filename)
{
	remove(filename);
	return SAL_OK;
}

s32 sal_StringCompare(const char *string1, const char *string2)
{
	s32 i=0;
	s8 c1=0,c2=0;
	while(1)
	{
		c1=string1[i];
		c2=string2[i];
		// check for string end
		
		if ((c1 == 0) && (c2 == 0)) return 0;
		if (c1 == 0) return -1;
		if (c2 == 0) return 1;
		
		if ((c1 >= 0x61)&&(c1<=0x7A)) c1-=0x20;
		if ((c2 >= 0x61)&&(c2<=0x7A)) c2-=0x20;
		if (c1>c2)
			return 1;
		else if (c1<c2)
			return -1;
		i++;
	}

}

const char * sal_DirectoryGetHome(void)
{
	static char home [SAL_MAX_PATH];

	snprintf(home, sizeof(home), "%s", "/documents/ndless/");
	sal_DirectoryCombine(home, ".pocketsnes");

	/* Create the directory if it didn't already exist */
	mkdir(home, 0755);
	
	return home;
}

const char * sal_DirectoryGetUser(void)
{
	return "/documents/ndless/";
}

void sal_DirectorySplitFilename(const char *wholeFilename, s8* path, s8 *filename, s8 *ext)
{
	u32 len=(u32)strlen(wholeFilename);
	s32 i=0,dot=-1,slash=-1;

	ext[0]=0;
	filename[0]=0;
	path[0]=0;
	
	//Check given string is not null
	if (len<=0)
	{
		return;
	}
	
	for(i=len-2;i>0;i--)
	{
		if ((wholeFilename[i]=='.') && (dot==-1))
		{
			dot=i;
		}
		
		if ((wholeFilename[i]==(s8)SAL_DIR_SEP[0]) && (slash==-1))
		{
			slash=i;
			break;
		}
	}

	//Did we find an extension
	if (slash>=0)
	{
		//a directory has been found
		memcpy(path,wholeFilename,slash);
		path[slash]=0;
	}
	
	if (dot>=0)
	{
		// /ppppppp/ppppppp/ffffff.eeeee
		//                 S      D
		memcpy(filename,wholeFilename+slash+1,dot-slash-1);
		filename[dot-slash-1]=0; // change "." to zero to end string
		memcpy(ext,wholeFilename+dot+1,len-dot);
		//ext[len-(y+1)+1]=0;
		ext[len-dot+1]=0;
	}
	else
	{
		memcpy(filename,wholeFilename+slash+1,len-slash);
		filename[len-slash]=0;
	}
}

void sal_DirectoryCombine(s8 *path, const char *name)
{
	s32 len = strlen(path);
	s32 i=0;

	if (len>0)
	{
		if((path[len-1] != SAL_DIR_SEP[0]))
		{
			path[len]=SAL_DIR_SEP[0];
			len++;
		}
	}
	while(1)
	{
		path[len]=name[i];
		if (name[i]==0) break;
		len++;
		i++;
	}
}




s32 sal_ImageDraw(u16 *image, u32 width, u32 height, s32 x, s32 y)
{	
	u16 *fbStart = (u16*)sal_VideoGetBuffer();
	u16 *fb;
	u16 *graphics = (u16*)image;
	u32 x2,y2;
	fbStart = (u16*) ((u8*) fbStart + (y * sal_VideoGetPitch())) + x;

	for (y2=0; y2<height; y2++)
	{
		fb=fbStart;
		for (x2=0; x2<width; x2++)
		{
			*fb++ = *graphics++;
		}
		fbStart = (u16*) ((u8*) fbStart + sal_VideoGetPitch());
	}

	return SAL_OK;
}
