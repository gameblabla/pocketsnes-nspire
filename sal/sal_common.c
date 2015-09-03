
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

static s32 mAudioRateLookup[] = {
	11025, 22050, 32000, 44100, 48000,
};

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

static void sal_VideoDrawRect8(s32 x, s32 y, s32 width, s32 height, u8 color)
{
	u8 *pixy = (u8*)sal_VideoGetBuffer();
	u8 *pixx;
	s32 h,w;
	pixy = pixy + y * sal_VideoGetPitch() + x;
	for(h=0;h<height;h++)
	{
		pixx=pixy;		
		for(w=0; w<width; w++)
		{
			*pixx++ = color;
		}
		pixy+=sal_VideoGetPitch();
	}
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
	if (mBpp==8) sal_VideoDrawRect8(x,y,width,height,(u8)color);
	else sal_VideoDrawRect16(x,y,width,height,(u16)color);
}

static void sal_VideoPrint8(s32 x, s32 y, const char *buffer, u8 color)
{
	s32 m,b;
	u8 *pix = (u8*)sal_VideoGetBuffer();
	s32 len=0;
	s32 maxLen=(sal_VideoGetWidth()>>3)-(x>>3);

	pix = pix + y * sal_VideoGetPitch() + x;
	while(1) 
	{
		s8 letter = *buffer++;
		u32 *offset;

		//Check for end of string
		if (letter == 0) break;

		//Get pointer to graphics for letter
		offset=mFont8x8+((letter)<<1);
		
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
				pix+=sal_VideoGetPitch(); //move to next line
				mask>>=8; //shift mask data ready for next loop
			}
		}
		//position pix pointer to start of next char
		pix-=(sal_VideoGetPitch()<<3);
		pix+=(1<<3);

		len++;
		if (len>=maxLen-1) break;
	}
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
		offset=mFont8x8+((letter)<<1);
		
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
	if (mBpp==8) sal_VideoPrint8(x,y,buffer,(u8)color);
	else sal_VideoPrint16(x,y,buffer,(u16)color);
}

static 
void sal_VideoClear16(u16 color)
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

static 
void sal_VideoClear8(u8 color)
{
	s32 x,y,w,h,pitch;
	u8 *pix=(u8*)sal_VideoGetBuffer();

	w = sal_VideoGetWidth();
	h = sal_VideoGetHeight();
	pitch = sal_VideoGetPitch();

	for (y=0;y<h;y++)
	{
		for (x=0;x<w;x++)
		{
			*pix++ = color;
		}
		pix += pitch - w;
	}
}

void sal_VideoClear(u32 color)
{
	if (mBpp==8) sal_VideoClear8((u8)color);
	else sal_VideoClear16((u16)color);
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

#if 0
void sal_Sleep(u32 milliSecs)
{
	u32 timerStart=sal_TimerRead();
	timerStart+=milliSecs;
	while(sal_TimerRead()<timerStart)
	{

	}
}
#endif

void sal_ZipGetFirstFilename(const char *filename, s8 *longfilename)
{
   FILE *fd = NULL;
   u8 romname_len=0;
   fd = (FILE*)fopen(filename, "rb");
   if(fd)
   {    
		fseek(fd, 26,SEEK_SET);
		if(fread((void*)&romname_len, 1, 1, fd)==1) // read 1 byte - name length
		{
			// now copy name string to te
			fseek(fd, 30, SEEK_SET);
			fread(longfilename,1,romname_len,fd);
			if(longfilename[romname_len-4] == '.')
			{
				longfilename[romname_len-1]=0;
				longfilename[romname_len-2]=0;
				longfilename[romname_len-3]=0;
				longfilename[romname_len-4]=0;
			}
		}
		fclose(fd);
    }
}
			
s32 sal_ZipGetFirstCrc(const char *filename, s32 *crc)
{
	FILE *fd = NULL;
	s32 retVal=SAL_OK;
   
	fd = (FILE*)fopen(filename, "rb");
	if(fd)
	{    
		fseek(fd, 14,SEEK_SET);
		if(fread((s8*)crc, 1, 4, fd)!=4) // read 1 byte - name length
		{
			retVal=SAL_ERROR;			
			*crc=0;
		}
		fclose(fd);
	}
    
    return retVal;
}

s32 sal_ZipLoad(const char *filename, s8 *buffer, s32 bufferMaxSize, s32 *file_size)
{
    s32 size = 0;
    s8 *buf = NULL;

    unzFile *fd = NULL;
    unz_file_info info;
    s32 ret = SAL_OK;
        
    /* Attempt to open the archive */
    fd = unzOpen(filename);
    if(!fd)
	{
		sal_LastErrorSet("Failed to open file");
		return SAL_ERROR;
	}

    /* Go to first file in archive */
    ret = unzGoToFirstFile(fd);
    if(ret != UNZ_OK)
    {
		sal_LastErrorSet("Failed to find first file in zip");
        unzClose(fd);
        return SAL_ERROR;
    }

    ret = unzGetCurrentFileInfo(fd, &info, NULL, 0, NULL, 0, NULL, 0);
    if(ret != UNZ_OK)
    {
		sal_LastErrorSet("Failed to zip info");
        unzClose(fd);
        return SAL_ERROR;
    }

    /* Open the file for reading */
    ret = unzOpenCurrentFile(fd);
    if(ret != UNZ_OK)
    {
		sal_LastErrorSet("Failed to read file");
        unzClose(fd);
        return SAL_ERROR;
    }

    /* Allocate file data buffer */
    size = info.uncompressed_size;
	if(size>bufferMaxSize)
	{
		sal_LastErrorSet("Rom exceeds buffer size limit");
		unzClose(fd);
		return SAL_ERROR;
	}
	buf = buffer;

    /* Read (decompress) the file */
    ret = unzReadCurrentFile(fd, buf, info.uncompressed_size);
    if((u32)ret != info.uncompressed_size)
    {
		sal_LastErrorSet("File failed to uncompress fully");
		unzCloseCurrentFile(fd);
        unzClose(fd);
        return SAL_ERROR;
    }

    /* Close the current file */
    ret = unzCloseCurrentFile(fd);
    if(ret != UNZ_OK)
    {
		sal_LastErrorSet("Failed to close file in zip");
        unzClose(fd);
        return SAL_ERROR;
    }

    /* Close the archive */
    ret = unzClose(fd);
    if(ret != UNZ_OK)
    {
		sal_LastErrorSet("Failed to close zip");
        return SAL_ERROR;
    }

    /* Update file size and return pointer to file data */
    *file_size = size;
    return SAL_OK;

}


/*
    Verifies if a file is a ZIP archive or not.
    Returns: 1= ZIP archive, 0= not a ZIP archive
*/
s32 sal_ZipCheck(const char *filename)
{
    u8 buf[2];
    FILE *fd = NULL;
    fd = (FILE*)fopen(filename, "rb");
    if(!fd) return SAL_FALSE;
    fread(buf, 1, 2, fd);
    fclose(fd);
    if(memcmp(buf, "PK", 2) == 0) return SAL_TRUE;
    return SAL_FALSE;
}

s32 sal_ZipSave(const char *filename, s8 *firstFilename, s8 *buffer, s32 size)
{
    zipFile *fd = NULL;
    s32 ret = 0;
    fd=zipOpen(filename,0);
    if(!fd)
    {
       sal_LastErrorSet("Failed to create zip");
       return SAL_ERROR;
    }

    ret=zipOpenNewFileInZip(fd,firstFilename,
			    NULL,
				NULL,0,
			    NULL,0,
			    NULL,
			    Z_DEFLATED,
			    Z_BEST_COMPRESSION);
			    
    if(ret != ZIP_OK)
    {
       zipClose(fd,NULL);
       sal_LastErrorSet("Failed to create file in zip");
       return SAL_ERROR;    
    }

    ret=zipWriteInFileInZip(fd,buffer,size);
    if(ret != ZIP_OK)
    {
      zipCloseFileInZip(fd);
      zipClose(fd,NULL);
      sal_LastErrorSet("Failed to write file in zip");
      return SAL_ERROR;
    }

    ret=zipCloseFileInZip(fd);
    if(ret != ZIP_OK)
    {
      zipClose(fd,NULL);
      sal_LastErrorSet("Failed to close file in zip");
      return SAL_ERROR;
    }

    ret=zipClose(fd,NULL);
    if(ret != ZIP_OK)
    {
      sal_LastErrorSet("Failed to close zip");
      return SAL_ERROR;
    }

    return SAL_OK;
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

	if (!home[0]) {
		char *env_home = getenv("HOME");
		strcpy(home, env_home);
		sal_DirectoryCombine(home, ".snes96_snapshots");

		/* Create the directory if it didn't already exist */
		mkdir(home, 0755);
	}
	return home;
}

const char * sal_DirectoryGetUser(void)
{
	return getenv("HOME");
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



s32 sal_ImageDrawTiled(u16 *image, u32 width, u32 height, s32 xScroll, s32 yScroll, s32 x, s32 y)
{	
	s16 x2=0, x3=0;
	s16 y2=0, y3=0;
	u16 *fbStart = (u16*)sal_VideoGetBuffer();
	u16 *fb;
	fbStart = (u16*) ((u8*) fbStart + (y * sal_VideoGetPitch())) + x;
	u16 *graphics1 = (u16 *)NULL;

	x2=xScroll;
	y2=(yScroll*width);
	graphics1 = image+y2;
	for (y3=y; y3<sal_VideoGetHeight(); y3++)
	{		
		fb=fbStart;
		for (x3=0; x3<(sal_VideoGetWidth()-x); x3++)
		{
			*fb++ = graphics1[x2];
			x2++;
			x2&=(width-1);
		}
		fbStart = (u16*) ((u8*) fbStart + sal_VideoGetPitch());
		y2+=width;
		y2&=((height*width)-1);
		graphics1=image+y2;
	}

	return SAL_OK;
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

s32 sal_HighlightBar(s32 width, s32 height, s32 x, s32 y)
{
	u16 *fbStart = (u16*)sal_VideoGetBuffer();
	u16 *blitStart = (u16*) ((u8*) fbStart + y * sal_VideoGetPitch()) + x;

	//vertical stride
	int v_stride = sal_VideoGetPitch() - (width * sizeof(u16));

	int percentage_stride = (1 << 16) / width;
	int percentage = 0;
	int percentage_inv = (1 << 16);

	int percentage_r_err = 0;

	int percentage_bg_r_err = 0;
	int percentage_bg_g_err = 0;
	int percentage_bg_b_err = 0;

	int x2, y2;
	for (y2=0; y2 < height; y2++)
	{
		for (x2=0; x2 < width; x2++)
		{
			int bg_col = *blitStart;
			int r, g, b, bgr, bgg, bgb;

			//extract background colours
			bgr = bg_col >> 11;
			bgg = (bg_col >> 6) & 0x1F;
			bgb = bg_col & 0x1F;

			//propagate the error
			percentage_bg_r_err += (percentage * bgr) & 0xFFFF;
			percentage_bg_g_err += (percentage * bgg) & 0xFFFF;
			percentage_bg_b_err += (percentage * bgb) & 0xFFFF;
			percentage_r_err += (percentage_inv * 31) & 0xFFFF;

			//final colour blend, dont need to do the inverse of g/b because we're only blending red with the background!
			r =  ((31 * percentage_inv) + (percentage_r_err & ~0xFFFF)) >> 16;
			r += ((bgr * percentage) + (percentage_bg_r_err & ~0xFFFF)) >> 16;
			g =  ((bgg * percentage) + (percentage_bg_g_err & ~0xFFFF)) >> 16;
			b =  ((bgb * percentage) + (percentage_bg_b_err & ~0xFFFF)) >> 16;

			*blitStart++ = SAL_RGB((r > 31) ? 31 : r, g, b);

			percentage += percentage_stride;
			percentage_inv -= percentage_stride;

			//If the error is > 1 (in fix16.16), cut it down to size
			percentage_r_err &= 0xFFFF;
			percentage_bg_r_err &= 0xFFFF;
			percentage_bg_g_err &= 0xFFFF;
			percentage_bg_b_err &= 0xFFFF;
		}

		blitStart = (u16*) ((u8*) blitStart + v_stride);
		percentage = 0;
		percentage_inv = (1 << 16);
	}

	return SAL_OK;
}

s32 sal_ImageLoad(const char *fname, void *dest, u32 width, u32 height)
{
	FILE *fp;
	png_structp png_ptr = NULL;
	png_infop info_ptr = NULL;
	png_bytepp row_ptr = NULL;

	if (dest == NULL)
	{
		sal_LastErrorSet("Null destination");
		return SAL_ERROR;
	}
	
	if (fname == NULL)
	{
		sal_LastErrorSet("Null filename");
		return SAL_ERROR;
	}

	fp = fopen(fname, "r");
	if (fp == NULL)
	{
		sal_LastErrorSet("image does not exist");
		return SAL_ERROR;
	}

	png_ptr = png_create_read_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
	if (!png_ptr)
	{
		sal_LastErrorSet("png_create_read_struct() failed");
		fclose(fp);
		return SAL_ERROR;
	}

	info_ptr = png_create_info_struct(png_ptr);
	if (!info_ptr)
	{
		sal_LastErrorSet("png_create_info_struct() failed");
		png_destroy_read_struct(&png_ptr, info_ptr ? &info_ptr : NULL, (png_infopp)NULL);
		fclose(fp);
		return SAL_ERROR;
	}

	// Start reading
	png_init_io(png_ptr, fp);
	
	png_read_png(png_ptr, info_ptr, PNG_TRANSFORM_STRIP_16 | PNG_TRANSFORM_STRIP_ALPHA | PNG_TRANSFORM_PACKING, NULL);
	row_ptr = png_get_rows(png_ptr, info_ptr);
	if (row_ptr == NULL)
	{
		sal_LastErrorSet("png_get_rows() failed");
		png_destroy_read_struct(&png_ptr, info_ptr ? &info_ptr : NULL, (png_infopp)NULL);
		fclose(fp);
		return SAL_ERROR;
	}

	u32 h;
	unsigned short *dst = dest;
	
	if (png_get_bit_depth(png_ptr, info_ptr) != 8 || png_get_color_type(png_ptr, info_ptr) != PNG_COLOR_TYPE_RGB)
	{
		sal_LastErrorSet("bg image not 24bpp");
		png_destroy_read_struct(&png_ptr, info_ptr ? &info_ptr : NULL, (png_infopp)NULL);
		fclose(fp);
		return SAL_ERROR;
	}
	
	if (height != png_get_image_height(png_ptr, info_ptr))
	{
		sal_LastErrorSet("image height invalid");
		png_destroy_read_struct(&png_ptr, info_ptr ? &info_ptr : NULL, (png_infopp)NULL);
		fclose(fp);
		return SAL_ERROR;
	}
	
	if (width != png_get_image_width(png_ptr, info_ptr))
	{
		sal_LastErrorSet("image width is invalid");
		png_destroy_read_struct(&png_ptr, info_ptr ? &info_ptr : NULL, (png_infopp)NULL);
		fclose(fp);
		return SAL_ERROR;
	}
	
	for (h = 0; h < height; h++)
	{
		unsigned char *src = row_ptr[h];
		int len = width;
		while (len--)
		{
			*dst++ = SAL_RGB(src[0]>>3, src[1]>>3, src[2]>>3);
			//*dst++ = ((src[0]&0xf8)<<8) | ((src[1]&0xf8)<<3) | (src[2] >> 3); // RGB
			src += 3;
		}
	}

	png_destroy_read_struct(&png_ptr, info_ptr ? &info_ptr : NULL, (png_infopp)NULL);
	fclose(fp);
	return SAL_OK;
}

u32 sal_AudioRateNext(u32 currRate)
{
	s32 x;
	u32 newRate;
	for (x=0;x<sizeof(mAudioRateLookup)/sizeof(mAudioRateLookup[0]);x++)
	{
		newRate=mAudioRateLookup[x];
		if(newRate>currRate) break;
	}
	return newRate;
}

u32 sal_AudioRatePrevious(u32 currRate)
{
	s32 x;
	u32 newRate;
	for (x=(sizeof(mAudioRateLookup)/sizeof(mAudioRateLookup[0]))-1; x>=0; x--)
	{
		newRate=mAudioRateLookup[x];
		if(newRate<currRate) break;
	}
	return newRate;
}

static u32 CRC32_table[256] =
	{
	0x00000000L, 0x77073096L, 0xee0e612cL, 0x990951baL, 0x076dc419L, 0x706af48fL, 0xe963a535L, 0x9e6495a3L,
	0x0edb8832L, 0x79dcb8a4L, 0xe0d5e91eL, 0x97d2d988L, 0x09b64c2bL, 0x7eb17cbdL, 0xe7b82d07L, 0x90bf1d91L,
	0x1db71064L, 0x6ab020f2L, 0xf3b97148L, 0x84be41deL, 0x1adad47dL, 0x6ddde4ebL, 0xf4d4b551L, 0x83d385c7L,
	0x136c9856L, 0x646ba8c0L, 0xfd62f97aL, 0x8a65c9ecL, 0x14015c4fL, 0x63066cd9L, 0xfa0f3d63L, 0x8d080df5L,
	0x3b6e20c8L, 0x4c69105eL, 0xd56041e4L, 0xa2677172L, 0x3c03e4d1L, 0x4b04d447L, 0xd20d85fdL, 0xa50ab56bL,
	0x35b5a8faL, 0x42b2986cL, 0xdbbbc9d6L, 0xacbcf940L, 0x32d86ce3L, 0x45df5c75L, 0xdcd60dcfL, 0xabd13d59L,
	0x26d930acL, 0x51de003aL, 0xc8d75180L, 0xbfd06116L, 0x21b4f4b5L, 0x56b3c423L, 0xcfba9599L, 0xb8bda50fL,
	0x2802b89eL, 0x5f058808L, 0xc60cd9b2L, 0xb10be924L, 0x2f6f7c87L, 0x58684c11L, 0xc1611dabL, 0xb6662d3dL,
	0x76dc4190L, 0x01db7106L, 0x98d220bcL, 0xefd5102aL, 0x71b18589L, 0x06b6b51fL, 0x9fbfe4a5L, 0xe8b8d433L,
	0x7807c9a2L, 0x0f00f934L, 0x9609a88eL, 0xe10e9818L, 0x7f6a0dbbL, 0x086d3d2dL, 0x91646c97L, 0xe6635c01L,
	0x6b6b51f4L, 0x1c6c6162L, 0x856530d8L, 0xf262004eL, 0x6c0695edL, 0x1b01a57bL, 0x8208f4c1L, 0xf50fc457L,
	0x65b0d9c6L, 0x12b7e950L, 0x8bbeb8eaL, 0xfcb9887cL, 0x62dd1ddfL, 0x15da2d49L, 0x8cd37cf3L, 0xfbd44c65L,
	0x4db26158L, 0x3ab551ceL, 0xa3bc0074L, 0xd4bb30e2L, 0x4adfa541L, 0x3dd895d7L, 0xa4d1c46dL, 0xd3d6f4fbL,
	0x4369e96aL, 0x346ed9fcL, 0xad678846L, 0xda60b8d0L, 0x44042d73L, 0x33031de5L, 0xaa0a4c5fL, 0xdd0d7cc9L,
	0x5005713cL, 0x270241aaL, 0xbe0b1010L, 0xc90c2086L, 0x5768b525L, 0x206f85b3L, 0xb966d409L, 0xce61e49fL,
	0x5edef90eL, 0x29d9c998L, 0xb0d09822L, 0xc7d7a8b4L, 0x59b33d17L, 0x2eb40d81L, 0xb7bd5c3bL, 0xc0ba6cadL,
	0xedb88320L, 0x9abfb3b6L, 0x03b6e20cL, 0x74b1d29aL, 0xead54739L, 0x9dd277afL, 0x04db2615L, 0x73dc1683L,
	0xe3630b12L, 0x94643b84L, 0x0d6d6a3eL, 0x7a6a5aa8L, 0xe40ecf0bL, 0x9309ff9dL, 0x0a00ae27L, 0x7d079eb1L,
	0xf00f9344L, 0x8708a3d2L, 0x1e01f268L, 0x6906c2feL, 0xf762575dL, 0x806567cbL, 0x196c3671L, 0x6e6b06e7L,
	0xfed41b76L, 0x89d32be0L, 0x10da7a5aL, 0x67dd4accL, 0xf9b9df6fL, 0x8ebeeff9L, 0x17b7be43L, 0x60b08ed5L,
	0xd6d6a3e8L, 0xa1d1937eL, 0x38d8c2c4L, 0x4fdff252L, 0xd1bb67f1L, 0xa6bc5767L, 0x3fb506ddL, 0x48b2364bL,
	0xd80d2bdaL, 0xaf0a1b4cL, 0x36034af6L, 0x41047a60L, 0xdf60efc3L, 0xa867df55L, 0x316e8eefL, 0x4669be79L,
	0xcb61b38cL, 0xbc66831aL, 0x256fd2a0L, 0x5268e236L, 0xcc0c7795L, 0xbb0b4703L, 0x220216b9L, 0x5505262fL,
	0xc5ba3bbeL, 0xb2bd0b28L, 0x2bb45a92L, 0x5cb36a04L, 0xc2d7ffa7L, 0xb5d0cf31L, 0x2cd99e8bL, 0x5bdeae1dL,
	0x9b64c2b0L, 0xec63f226L, 0x756aa39cL, 0x026d930aL, 0x9c0906a9L, 0xeb0e363fL, 0x72076785L, 0x05005713L,
	0x95bf4a82L, 0xe2b87a14L, 0x7bb12baeL, 0x0cb61b38L, 0x92d28e9bL, 0xe5d5be0dL, 0x7cdcefb7L, 0x0bdbdf21L,
	0x86d3d2d4L, 0xf1d4e242L, 0x68ddb3f8L, 0x1fda836eL, 0x81be16cdL, 0xf6b9265bL, 0x6fb077e1L, 0x18b74777L,
	0x88085ae6L, 0xff0f6a70L, 0x66063bcaL, 0x11010b5cL, 0x8f659effL, 0xf862ae69L, 0x616bffd3L, 0x166ccf45L,
	0xa00ae278L, 0xd70dd2eeL, 0x4e048354L, 0x3903b3c2L, 0xa7672661L, 0xd06016f7L, 0x4969474dL, 0x3e6e77dbL,
	0xaed16a4aL, 0xd9d65adcL, 0x40df0b66L, 0x37d83bf0L, 0xa9bcae53L, 0xdebb9ec5L, 0x47b2cf7fL, 0x30b5ffe9L,
	0xbdbdf21cL, 0xcabac28aL, 0x53b39330L, 0x24b4a3a6L, 0xbad03605L, 0xcdd70693L, 0x54de5729L, 0x23d967bfL,
	0xb3667a2eL, 0xc4614ab8L, 0x5d681b02L, 0x2a6f2b94L, 0xb40bbe37L, 0xc30c8ea1L, 0x5a05df1bL, 0x2d02ef8dL
	};

u32 sal_FileGetCRC(u8 *data, u32 size)
{
	u32 x,CRC=0;
	u8 b=0;

	for(x=0;x<size;x++)
	{
		b=data[x];
		CRC=(CRC32_table[((CRC^0xffffffffL)^(b))&0xff]^((CRC^0xffffffffL))>>8)^0xffffffffL;

	}

	return CRC;
}
