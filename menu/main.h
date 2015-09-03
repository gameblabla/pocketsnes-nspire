#define MAX_LENGH 1024
#define EXECUTE_APP

#define TITLE_WINDOW "PocketSNES"

#define FORMAT_FILE ".sfc"
#define EXECUTABLE_NAME "pocketsnes.tns"
#define DEFAULT_TEXT "Select a Super Nintendo Rom file"
#define FILE_DELETED "File deleted !"

#define CONFIRM_DELETE_FOLDER "Do you want to delete this folder ?"
#define CONFIRM_DELETE_FILE "Do you want to delete this file ?"

#define YES_CHOOSE "YES"
#define NO_CHOOSE "NO"

/*	Colors	*/
#define BLUE_C  255
#define RED_C  1700
#define GREEN_C 512
#define TUR_C 750
#define F_C  1023

#ifdef EXECUTE_APP
	#define tostart mainEntry
	int mainEntry(char* romname);
#endif

#ifdef ndlib

#define PAD_UP			isKeyPressed(KEY_NSPIRE_UP)
#define PAD_DOWN		isKeyPressed(KEY_NSPIRE_DOWN)
#define PAD_LEFT		isKeyPressed(KEY_NSPIRE_LEFT)
#define PAD_RIGHT		isKeyPressed(KEY_NSPIRE_RIGHT)

#define PAD_CONFIRM		isKeyPressed(KEY_NSPIRE_CTRL)
#define PAD_CONFIRM2	isKeyPressed(KEY_NSPIRE_ENTER)
#define PAD_DELETE		isKeyPressed(KEY_NSPIRE_DEL)
#define PAD_QUIT		isKeyPressed(KEY_NSPIRE_ESC)

#else

#define PAD_UP			keystate[SDLK_UP]
#define PAD_DOWN		keystate[SDLK_DOWN]
#define PAD_LEFT		keystate[SDLK_LEFT]
#define PAD_RIGHT		keystate[SDLK_RIGHT]

#define PAD_CONFIRM		keystate[SDLK_LCTRL]
#define PAD_CONFIRM2	keystate[SDLK_RETURN]
#define PAD_DELETE		keystate[SDLK_BACKSPACE]
#define PAD_QUIT		keystate[SDLK_ESCAPE]

#endif

void init (void);
void controls ();

void refresh_cursor(unsigned char all);
void draw_files_list();

void screen_showchar(int x, int y, unsigned char a, int fg_color, int bg_color);
void print_string(char *s, unsigned short fg_color, unsigned short bg_color, int x, int y);

unsigned char is_folder(char* str1);
void list_all_files(char* directory);

void goto_folder();
void remove_file();
unsigned char clear_dir(char* which_dir);

void set_fileid();
