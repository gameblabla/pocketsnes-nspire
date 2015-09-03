
#include <string.h>
//#include <error.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <dirent.h>
#include "sal.h"


s32 sal_DirectoryGetCurrent(s8 *path, u32 size)
{
	getcwd(path,size);
	return SAL_OK;
}

s32 sal_DirectoryCreate(const char *path)
{
	s32 count=0;
	mkdir(path, 0x777);
	return SAL_OK;
}

s32 sal_DirectoryGetItemCount(const char *path, s32 *returnItemCount)
{
	u32 count=0;
	DIR *d;
	struct dirent *de;

	d = opendir(path);

	if (d)
	{
		while ((de = readdir(d)))
		{
			count++;
		}

	}
	else
		return SAL_ERROR;

	*returnItemCount=count;
	return SAL_OK;
}

s32 sal_DirectoryOpen(const char *path, struct SAL_DIR *d)
{
	d->dir=opendir(path);

	if(d->dir) return SAL_OK;
	else return SAL_ERROR;
}

s32 sal_DirectoryClose(struct SAL_DIR *d)
{
	if(d)
	{
		if(d->dir)
		{
			closedir(d->dir);
			d->dir=NULL;
			return SAL_OK;
		}
		else
		{
			return SAL_ERROR;
		}
	}
	else
	{
		return SAL_ERROR;
	}
}

s32 sal_DirectoryRead(struct SAL_DIR *d, struct SAL_DIRECTORY_ENTRY *dir)
{
	struct dirent *de=NULL;

	if(d)
	{
		if(dir)
		{
			de=readdir(d->dir);
			if(de)
			{
				strcpy(dir->filename,de->d_name);
				strcpy(dir->displayName,de->d_name);
				if (de->d_type == 4)
				  dir->type=SAL_FILE_TYPE_DIRECTORY;
				else
				  dir->type=SAL_FILE_TYPE_FILE;
				return SAL_OK;
			}
			else
			{
				return SAL_ERROR;
			}
		}
		else
		{
			return SAL_ERROR;
		}
	}
	else
	{
		return SAL_ERROR;
	}
}

s32 sal_DirectoryGet(const char *path, struct SAL_DIRECTORY_ENTRY *dir,
			s32 startIndex, s32 count)
{
	s32 fileCount=0;
	DIR *d;
	struct dirent *de;
	uint32_t entriesRead=0;
	
	char fullFilename[256];
	s32 endIndex=startIndex+count;
	long loc;

	d = opendir(path);

	if (d)
	{
		/*loc=telldir(d);*/
		/*seekdir(d,loc+startIndex);*/
		while ((de = readdir(d)))
		{
			if(startIndex >= endIndex)
			{
				//exit loop
				break;
			}

			//Is entry a file or directory
			if (de->d_type == 4) // Directory
			{
				strcpy(dir[fileCount].filename,de->d_name);
				strcpy(dir[fileCount].displayName,de->d_name);
				dir[fileCount].type=SAL_FILE_TYPE_DIRECTORY;
			}
			else
			{
				//File
				strcpy(dir[fileCount].filename,de->d_name);
				strcpy(dir[fileCount].displayName,de->d_name);
				dir[fileCount].type=SAL_FILE_TYPE_FILE;
			}
			fileCount++;
			startIndex++;
		}
		closedir(d);
	}
	return SAL_ERROR;
//	return SAL_OK;
}



void sal_DirectoryGetParent(s8 *path)
{
	int i;
	unsigned int len = strlen(path);

	/* Strip trailing slashes */
	for (i = len - 1; i && (path[i] == SAL_DIR_SEP[0] ||
					path[i] == SAL_DIR_SEP_BAD[0]); i--)
		path[i] = '\0';

	for (; i && path[i] != SAL_DIR_SEP[0] && path[i] != SAL_DIR_SEP_BAD[0]; i--);
	if (i)
		path[i] = '\0';
	else
		path[i + 1] = '\0';
}
