/*  
  COPYRIGHT (C) 2015, Bruno Marie 
  (aka Gameblabla)
  
  This program is free software; you can redistribute it and/or
  modify it under the terms of the GNU General Public License
  as published by the Free Software Foundation; either version 3
  of the License, or (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the

  You should have received a copy of the GNU General Public License
  along with this program; if not, write to the Free Software
  Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
*/

/*
 * nFile Browser 
 * Simple file browser mainly targetting the TI nspire using SDL/n2DLib.
 * It can also be used on other platforms as a simple file browser.
 * On TI Nspire, it can launch Ndless applications. (only using the SDL port though)
*/

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <dirent.h>
#include <sys/types.h>
#include <sys/stat.h>

#ifndef ndlib
	#include <SDL/SDL.h>
#else
	#include "n2DLib.h"
#endif

#ifdef _TINSPIRE
	#include <nucleus.h>
	#include <libndls.h>
#endif

#include "data.h"
#include "main.h"

#ifdef ndlib
	struct rectangle
	{
		unsigned short x;
		unsigned short y;
		unsigned short w;
		unsigned short h;
	} gui_dst, gui_dst2;
#else
	SDL_Surface *gui_screen;
	SDL_Rect gui_dst, gui_dst2;
	SDL_Event gui_event;
#endif

char file_name[MAX_LENGH][512];
short file_type[MAX_LENGH];

unsigned char button_state[8], button_time[8];

char* currentdir;
char* last_folder;

/* This was done in order to save some memory on limited platforms*/
unsigned short fileid_selected;
unsigned short choice;
unsigned short scroll_choice;
unsigned short numb_files;

void clear_entirescreen();
void update_entirescreen();


void init(void) 
{
#ifdef ndlib
	initBuffering();
	clearBufferB();
	updateScreen();
#else

	if (!SDL_WasInit(SDL_INIT_VIDEO)) 
	{	
		SDL_Init(SDL_INIT_VIDEO);
	}
	
	#ifdef _TINSPIRE
		gui_screen = SDL_SetVideoMode(320, 240, has_colors ? 16 : 8, SDL_SWSURFACE ); 
	#else
		gui_screen = SDL_SetVideoMode(320, 240, 16, SDL_SWSURFACE ); 
	#endif
	
	SDL_ShowCursor(SDL_DISABLE);
	SDL_WM_SetCaption(TITLE_WINDOW, NULL);
#endif

    /* Set the size of the clearing area. x, w, h never change so they are set here */	
	gui_dst.x = 16;
	gui_dst.w = 16;
	gui_dst.h = 48;
	
    /* Used to define the whole screen to be cleared */	
	gui_dst2.x = 0;
	gui_dst2.y = 0;
	gui_dst2.w = 320;
	gui_dst2.h = 240;
	
	choice = 0;
	
	/* scroll_choice = 0 means that files from id 0 to 11 (file_name) will be shown on screen  */	
	scroll_choice = 0;
}

int main(int argc, char* argv[]) 
{
	/* Buffer to hold =>the current directory */
	char* buf = NULL;
	unsigned char exit_app = 0;
#ifdef EXECUTE_APP
	unsigned char file_chosen;
	unsigned char done = 1;
#endif
	
	/* Temporary array, used for starting executable*/
	char file_to_start[MAX_LENGH];
	
	/* Init video and variables */
	init();
	
	clear_entirescreen();
	
	/* Set it to the current directory. */
	currentdir = getcwd(buf, MAX_LENGH);
	
	/* List 12 files to be shown on screen (here, it is the first chunck) */
	list_all_files(currentdir);
	
	update_entirescreen();
	
	/* Refresh everything on screen */
	refresh_cursor(1);
	
	while (exit_app==0) 
	{
		while (button_state[6]<1)
		{
			/* Call function for input */
			controls();
			
				/* If Up button is pressed down... (or Left button held) */
				if (button_state[0] == 1 || button_state[2] > 0)
				{
					if (choice > 0) 
					{
						choice--;
						refresh_cursor(0);
						set_fileid();
					}
					else if (scroll_choice > 0)
					{
						choice = 11;
						scroll_choice = scroll_choice - 1;
						refresh_cursor(3);
						set_fileid();
					}
				}
				/* If Down button is pressed down... (or Right button held) */
				else if (button_state[1] == 1 || button_state[3] > 0)
				{
					/* Don't let the user to scroll more than there are files... */
					if (fileid_selected < numb_files)
					{
						if (choice < 11) 
						{
							choice++;	
							refresh_cursor(0);
							set_fileid();
						}
						/* If the user wants to go down and there are more files, change the files to shown to the next one (thanks to scroll_choice) */
						else if (numb_files > 10)
						{
							scroll_choice = scroll_choice + 1;
							choice = 0;
							set_fileid();
							refresh_cursor(3);
						}
					}
				}
			
				if (button_state[6] == 1)
				{
					#ifdef EXECUTE_APP
						file_chosen = 0;
					#endif
					exit_app = 1;
				}
			
			
				/* If Control/Return button is pressed... */
				if (button_state[4]==1 || button_state[5]==1)
				{
					/* If file is a tns file then launch it */
					
					if (file_type[fileid_selected] == BLUE_C) 
					{
						snprintf(file_to_start, MAX_LENGH, "%s/%s", currentdir, file_name[fileid_selected]);
						#ifdef EXECUTE_APP
							file_chosen = 1;
							button_state[6] = 1;
						#elif defined(_TINSPIRE)
							nl_exec(file_to_start, 0, NULL);
						#endif
					}
					/* If not then it is a folder, if thats the case then go to that folder */
					else if (file_type[fileid_selected] == F_C || choice == 0) 
					{
						goto_folder();
					}
				
				}
			
			/* Don't waste CPU cycles */
			#ifndef ndlib
				SDL_Delay(16);
			#endif
		}
	
		#ifdef EXECUTE_APP
			if (file_chosen == 1)
			{
				#ifdef ndlib
					deinitBuffering();
				#else
					if (gui_screen) SDL_FreeSurface(gui_screen);
				#endif
					
				while(done == 1)
				{
					tostart(file_to_start);
					done = 0;
				}
					
				done = 1;
				file_chosen = 0;
				
				init();
				
				clear_entirescreen();
				currentdir = getcwd(buf, MAX_LENGH);
				list_all_files(currentdir);
				update_entirescreen();
				refresh_cursor(2);
			}
			else
			{
				exit_app = 1;
			}
		#endif
	}


#ifdef ndlib
	clearBufferB();
	deinitBuffering();
#else
	if (gui_screen != NULL) SDL_FreeSurface(gui_screen);
	SDL_QuitSubSystem(SDL_INIT_VIDEO);
	SDL_Quit();
#endif

	exit(0);
}

void controls()
{
#ifndef ndlib
    Uint8 *keystate = SDL_GetKeyState(NULL);
#endif
	int pad;
	unsigned char i;

	/*	Pressure buttons
	 *  0 means Inactive
	 *  1 means that the button was just pressed
	 *  2 means that the button is being held
	 *  3 means RELEASE THE BOGUS
	*/
	
	/*
	 * 0: UP
	 * 1: Down
	 * 2: Left
	 * 3: Right
	 * 4: Confirm
	 * 5: Confirm2
	 * 6: Quit
	*/

	for(i=0;i<sizeof(button_state);i++)
	{
		switch (i)
		{
			case 0:
			pad = PAD_UP;
			break;
			case 1:
			pad = PAD_DOWN;
			break;
			case 2:
			pad = PAD_LEFT;
			break;
			case 3:
			pad = PAD_RIGHT;
			break;
			case 4:
			pad = PAD_CONFIRM;
			break;
			case 5:
			pad = PAD_CONFIRM2;
			break;
			case 6:
			pad = PAD_QUIT;
			break;
		}
		
		switch (button_state[i])
		{
			case 0:
				if (pad)
				{
					button_state[i] = 1;
					button_time[i] = 0;
				}
			break;
			
			case 1:
				button_time[i] = button_time[i] + 1;
				
				if (button_time[i] > 0)
				{
					button_state[i] = 2;
					button_time[i] = 0;
				}
			break;
			
			case 2:
				if (!(pad))
				{
					button_state[i] = 3;
					button_time[i] = 0;
				}
			break;
			
			case 3:
				button_time[i] = button_time[i] + 1;
				
				if (button_time[i] > 1)
				{
					button_state[i] = 0;
					button_time[i] = 0;
				}
			break;
		}
		
	}

#ifndef ndlib
    SDL_Event event;
    SDL_PollEvent(&event);
#endif
}

void set_fileid()
{
	/* fileid_selected is the id of the file selected (first dimension of the file_name array) */
	fileid_selected = choice + (scroll_choice*12);
}


void goto_folder()
{
	#ifndef _TINSPIRE
		char buf[MAX_LENGH];
	#endif
	
#ifdef _TINSPIRE
	snprintf(currentdir, MAX_LENGH, "%s%s", currentdir, file_name[fileid_selected]);
#else
	snprintf(currentdir, MAX_LENGH, "%s/%s", currentdir, file_name[fileid_selected]);
#endif
	
	#ifdef _TINSPIRE
		/* Set the current directory (with lots of "..") to the chosen one */
		NU_Set_Current_Dir(currentdir);
		/* This will return the current directory properly */
		NU_Current_Dir("A:",currentdir);
	#else
		/* Set it to the current directory. */
		chdir(currentdir);
		/* Get current directory. */
		currentdir = getcwd(buf, MAX_LENGH);
	#endif
	
	list_all_files(currentdir);
	refresh_cursor(2);
}

/*
	Draw the list of files on-screen (it is divided by the scroll_choice variable)
	For example, scroll_choice = 1 means that files from id 12 to 23 will be shown etc...
*/
void draw_files_list()
{
	unsigned char i;
	
	for (i = 0; i < 12; i++)
	{
		print_string(file_name[i+(scroll_choice*12)],file_type[i+(scroll_choice*12)],0,48,40+(16*i));
	}
}

void refresh_cursor(unsigned char all)
{	
	if (all == 2)
	{
		choice = 0;
		scroll_choice = 0;
		set_fileid();
		clear_entirescreen();
	}
	else if (all == 3 || all == 4)
	{
		clear_entirescreen();
	}
	
	/* Update position of cursor so we can limit the area to refresh*/
	gui_dst.y = 40+(16*choice) - 16;
	
#ifdef ndlib
	fillRect(gui_dst.x, gui_dst.y, gui_dst.w, gui_dst.h, 0);
#else
	SDL_FillRect(gui_screen, &gui_dst, 0);
#endif
	
	/* Then draw the cursor again after being cleared, it will be shown after we passed SDL_Flip/UpdateRect to it*/
	print_string("=>",RED_C,0,16,40+(choice*16) );
	
	draw_files_list();
	
	if (all == 1 || all == 2 || all == 3)
	{
		print_string(currentdir,BLUE_C,0,8,16);
		print_string(DEFAULT_TEXT,1200,0,8,6 );
		print_string(CREDITS_TEXT,1200,0,8,232 );
		
		update_entirescreen();
	}
	else
	{
#ifdef ndlib
		updateScreen();
#else	
		SDL_UpdateRect(gui_screen, gui_dst.x, gui_dst.y, gui_dst.w, gui_dst.h);
#endif
	}
}

void clear_entirescreen()
{
	#ifdef ndlib
		clearBufferB();
	#else
		SDL_FillRect(gui_screen, &gui_dst2, 0);
	#endif
}

void update_entirescreen()
{
	#ifdef ndlib
		updateScreen();
	#else	
		SDL_Flip(gui_screen);
	#endif
}


void list_all_files(char* directory)
{
	DIR *dir;
	struct dirent *ent;
	
	unsigned char temp;
	
	#if MAX_LENGH < 255
		unsigned char i;
	#elif MAX_LENGH < 65000
		unsigned short i;
	#else
		unsigned long i;
	#endif

	short pc;
	
	/* Reset all the stored files to zero */
	for(i=0;i<MAX_LENGH;i++)
	{
		strcpy(file_name[i], "");
	}
	
	i = 0;
	temp = 0;
	pc = -1;
	numb_files = 0;
	
	if ((dir = opendir (directory)) != NULL) 
	{
		while ( (ent = readdir (dir)) != NULL ) 
		{
			/* Add the .. string and then after, reject them all*/
			if (i == 0)
			{
				strcpy(file_name[i], "..");
				file_type[i] = TUR_C;
				i++;
				numb_files++;
			}
			
			/* Finds .tns occurence */
#ifdef _TINSPIRE
			char* pch = strstr (ent->d_name,".tns");
#else
			char* pch = strstr (ent->d_name,FORMAT_FILE);
#endif
			
			/* Reject these two signs and the executable itself */
			char* pch2 = strstr (ent->d_name,"..");
			char* pch3 = strstr (ent->d_name,EXECUTABLE_NAME);
			pc = strncmp (ent->d_name, ".", 2);
			
			/* Check if file in question is a folder */
			temp = is_folder(ent->d_name);
			
			/* If file has ".tns" extension, is a folder and is not ".." and "." */
			if ((pch2 == NULL && pch3 == NULL && pc != 0))
			{
				/* Copy string cotent from ent->d_name to file_name[i]*/
				strcpy(file_name[i], ent->d_name);
				
				if (pch != NULL)
				{
					file_type[i] = BLUE_C;
				}
				else if (temp)
				{
					file_type[i] = F_C;
				}
				else
				{
					file_type[i] = GREEN_C;
				}

				i++;
				numb_files++;
			}
			
		}
		closedir (dir);
	} 
	else 
	{
		perror ("Not a directory");
	}
	
	numb_files = numb_files - 1;
}

/* Draw a char on-screen. Thanks Exphophase for the code ! (coming form Gpsp, GPLv2) */
void screen_showchar(int x, int y, unsigned char a, int fg_color, int bg_color) 
{
#ifdef ndlib
	drawChar(&x, &y, 0, a, fg_color, bg_color);
#else
	unsigned short *dst;
	int w, h;

	SDL_LockSurface(gui_screen);
	
	for(h = 8; h; h--) 
	{
		dst = (unsigned short *)gui_screen->pixels + (y+8-h)*gui_screen->w + x;
		for(w = 8; w; w--) 
		{
			unsigned short color = *dst; // background
			if((fontdata8x8[a*8 + (8-h)] >> w) & 1) color = fg_color;
			*dst++ = color;
		}
	}
	
	SDL_UnlockSurface(gui_screen);
#endif
}

/* Draw a string on-screen. Thanks Exphophase for the code ! (coming form Gpsp, GPLv2) */
void print_string(char *s, unsigned short fg_color, unsigned short bg_color, int x, int y) 
{
#ifdef ndlib
	drawString(&x, &y, 0, s, fg_color, bg_color);
#else
	int i;
	int j;
	j = strlen(s);
	
	for(i = 0; i < j; i++, x += 6) 
	{
		screen_showchar(x, y, s[i], fg_color, bg_color);
	}
#endif
}

/* Is the path a folder ? */
unsigned char is_folder(char* str1)
{
	struct stat st;
	unsigned char temp;
	
	if( stat( str1, &st ) == 0 && S_ISDIR( st.st_mode ) ) 
	{
		temp = 1;
	} 
	else 
	{
		temp = 0;
	}
	 
	return temp;
}
