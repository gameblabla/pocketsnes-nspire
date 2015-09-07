#ifndef INCLUDE_GRAFX
#define INCLUDE_GRAFX

#include <os.h>
#include <stdarg.h>

#define min(a, b) ((a) < (b) ? (a) : (b))
#define max(a, b) ((a) > (b) ? (a) : (b))

typedef int Fixed;
typedef struct
{
	int x;
	int y;
	int h;
	int w;
} Rect;

#define itofix(x) ((x) << 8)
#define fixtoi(x) ((x) >> 8)
#define fixdiv(x, y) (((x) << 8) / (y))
#define clamp(x, a, b) min(max(x, a), b)
#define sign(x) ((x) < 0)
#define fixsin(x) fixcos((x) - 64)

#ifdef __cplusplus
extern "C" {
#endif

extern void initBuffering();
extern void updateScreen();
extern void deinitBuffering();
extern void clearBufferB();
extern unsigned short getPixel(const unsigned short*, unsigned int, unsigned int);
extern void setPixelUnsafe(unsigned int, unsigned int, unsigned short);
extern void setPixel(unsigned int, unsigned int, unsigned short);
extern void setPixelRGB(unsigned int, unsigned int, unsigned char, unsigned char, unsigned char);
extern void drawHLine(int, int, int, unsigned short);
extern void drawVLine(int, int, int, unsigned short);
extern void fillRect(int, int, int, int, unsigned short);
extern void drawLine(int, int, int, int, unsigned short);
extern void drawString(int*, int*, int, const char*, unsigned short, unsigned short);
extern void drawChar(int*, int*, int, char, unsigned short, unsigned short);
extern int numberWidth(int);
extern int stringWidth(const char*);
extern int get_key_pressed(t_key*);
extern int isKey(t_key, t_key);

#define BUFF_BYTES_SIZE (320*240*2)
extern unsigned short *BUFF_BASE_ADDRESS;
#ifdef __cplusplus
}
#endif

#endif
