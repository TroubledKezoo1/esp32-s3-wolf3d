#include "wl_def.h"
#define screen screenBuffer


pictabletype	*pictable;
VL_Surface     *latchpics[NUMLATCHPICS];

int	    px,py;
byte	fontcolor,backcolor;
int	    fontnumber;

//==========================================================================

void VWB_DrawPropString(const char* string)
{
	fontstruct  *font;
	int		    width, step, height;
	byte	    *source, *dest;
	byte	    ch;
	int i;
	unsigned sx, sy;

	byte *vbuf = VL_LockSurface(curSurface);
	if(vbuf == NULL) return;

	void *p = grsegs[STARTFONT+fontnumber];
	if (!p) return;
	font = (fontstruct *) p;
	height = font->height;
	dest = vbuf + scaleFactor * (py * curPitch + px);

	while ((ch = (byte)*string++)!=0)
	{
		width = step = font->width[ch];
		source = ((byte *)font)+font->location[ch];
		while (width--)
		{
			for(i=0; i<height; i++)
			{
				if(source[i*step])
				{
					for(sy=0; sy<scaleFactor; sy++)
						for(sx=0; sx<scaleFactor; sx++)
							dest[(scaleFactor*i+sy)*curPitch+sx]=fontcolor;
				}
			}

			source++;
			px++;
			dest+=scaleFactor;
		}
	}

	VL_UnlockSurface(curSurface);
}

/*
=================
=
= VL_MungePic
=
=================
*/

void VL_MungePic (byte *source, unsigned width, unsigned height)
{
	unsigned x,y,plane,size,pwidth;
	byte *temp, *dest, *srcline;

	size = width*height;

	if (width&3)
		Quit ("VL_MungePic: Not divisable by 4!");

//
// copy the pic to a temp buffer
//
	temp = (byte *) heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!temp) temp = (byte *) malloc(size);
    CHECKMALLOCRESULT(temp);
	memcpy (temp,source,size);

//
// munge it back into the original buffer
//
	dest = source;
	pwidth = width/4;

	for (plane=0;plane<4;plane++)
	{
		srcline = temp;
		for (y=0;y<height;y++)
		{
			for (x=0;x<pwidth;x++)
				*dest++ = *(srcline+x*4+plane);
			srcline+=width;
		}
	}

	free(temp);
}

void VWL_MeasureString (const char *string, uint16_t *width, uint16_t *height, fontstruct *font)
{
	*height = font->height;
	for (*width = 0;*string;string++)
		*width += font->width[*((byte *)string)];	// proportional width
}

void VW_MeasurePropString (const char *string, uint16_t *width, uint16_t *height)
{
	void *p = grsegs[STARTFONT+fontnumber];
	if (!p) return;
	VWL_MeasureString(string,width,height,(fontstruct *)p);
}

/*
=============================================================================

				Double buffer management routines

=============================================================================
*/

void VH_UpdateScreen()
{
	//SDL_BlitSurface(screenBuffer, NULL, screen, NULL);
	VL_Flip();
}


void VWB_DrawTile8 (int x, int y, int tile)
{
	LatchDrawChar(x,y,tile);
}

void VWB_DrawTile8M (int x, int y, int tile)
{
	if (!grsegs[STARTTILE8M]) return;
	VL_MemToScreen (((byte *)grsegs[STARTTILE8M])+tile*64,8,8,x,y);
}

void VWB_DrawPic (int x, int y, int chunknum)
{
	int	picnum = chunknum - STARTPICS;
	unsigned width,height;

	x &= ~7;

	width = pictable[picnum].width;
	height = pictable[picnum].height;

	VL_MemToScreen (grsegs[chunknum],width,height,x,y);
}

void VWB_DrawPicScaledCoord (int scx, int scy, int chunknum)
{
	int	picnum = chunknum - STARTPICS;
	unsigned width,height;

	width = pictable[picnum].width;
	height = pictable[picnum].height;

    VL_MemToScreenScaledCoord (grsegs[chunknum],width,height,scx,scy);
}


void VWB_Bar (int x, int y, int width, int height, int color)
{
	VW_Bar (x,y,width,height,color);
}

void VWB_Plot (int x, int y, int color)
{
    if(scaleFactor == 1)
        VW_Plot(x,y,color);
    else
        VW_Bar(x, y, 1, 1, color);
}

void VWB_Hlin (int x1, int x2, int y, int color)
{
    if(scaleFactor == 1)
    	VW_Hlin(x1,x2,y,color);
    else
        VW_Bar(x1, y, x2-x1+1, 1, color);
}

void VWB_Vlin (int y1, int y2, int x, int color)
{
    if(scaleFactor == 1)
		VW_Vlin(y1,y2,x,color);
    else
        VW_Bar(x, y1, 1, y2-y1+1, color);
}


/*
=============================================================================

						WOLFENSTEIN STUFF

=============================================================================
*/

/*
=====================
=
= LatchDrawPic
=
=====================
*/

void LatchDrawPic (unsigned x, unsigned y, unsigned picnum)
{
	VL_LatchToScreen (latchpics[2+picnum-LATCHPICS_LUMP_START], x*8, y);
}

void LatchDrawPicScaledCoord (unsigned scx, unsigned scy, unsigned picnum)
{
	VL_LatchToScreenScaledCoord (latchpics[2+picnum-LATCHPICS_LUMP_START], scx*8, scy);
}


//==========================================================================

void FreeLatchMem()
{
    int i;
    for(i = 0; i < 2 + LATCHPICS_LUMP_END - LATCHPICS_LUMP_START; i++)
    {
        //SDL_FreeSurface(latchpics[i]);
        latchpics[i] = NULL;
    }
}

/*
===================
=
= LoadLatchMem
=
===================
*/

static VL_Surface* CreateSurface(int width, int height) {
    VL_Surface *surf = (VL_Surface*)malloc(sizeof(VL_Surface));
    if (!surf) Quit("Out of memory for VL_Surface");
    surf->w = width;
    surf->h = height;
    surf->pitch = width;
    
    int size = width * height;
    if (size == 0) size = 1; // Prevent malloc(0) returning NULL
    
    Serial.printf("CreateSurface: w=%d, h=%d, size=%d\n", width, height, size);
    
    // Try PSRAM first to save precious SRAM
    surf->pixels = heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!surf->pixels) {
        surf->pixels = malloc(size);
    }
    
    if (!surf->pixels) Quit("Out of memory for surface pixels (size=%d, w=%d, h=%d)", size, width, height);
    return surf;
}

void LoadLatchMem (void)
{
	int	i,width,height,start,end;
	byte *src;
	VL_Surface *surf;

//
// tile 8s
//
    surf = CreateSurface(8*8, ((NUMTILE8+7)/8)*8);

	latchpics[0] = surf;
	CA_CacheGrChunk (STARTTILE8);
	src = grsegs[STARTTILE8];

	for (i=0;i<NUMTILE8;i++)
	{
		VL_MemToLatch (src, 8, 8, surf, (i & 7) * 8, (i >> 3) * 8);
		src += 64;
	}
	UNCACHEGRCHUNK (STARTTILE8);

	latchpics[1] = NULL;  // not used

//
// pics
//
	start = LATCHPICS_LUMP_START;
	end = LATCHPICS_LUMP_END;

	for (i=start;i<=end;i++)
	{
		width = pictable[i-STARTPICS].width;
		height = pictable[i-STARTPICS].height;
        surf = CreateSurface(width, height);

		latchpics[2+i-start] = surf;
		CA_CacheGrChunk (i);
		VL_MemToLatch (grsegs[i], width, height, surf, 0, 0);
		UNCACHEGRCHUNK(i);
	}
}

//==========================================================================

/*
===================
=
= FizzleFade
=
= returns true if aborted
=
= It uses maximum-length Linear Feedback Shift Registers (LFSR) counters.
= You can find a list of them with lengths from 3 to 168 at:
= http://www.xilinx.com/support/documentation/application_notes/xapp052.pdf
= Many thanks to Xilinx for this list!!!
=
===================
*/

// XOR masks for the pseudo-random number sequence starting with n=17 bits
static const uint32_t rndmasks[] = {
                    // n    XNOR from (starting at 1, not 0 as usual)
    0x00012000,     // 17   17,14
    0x00020400,     // 18   18,11
    0x00040023,     // 19   19,6,2,1
    0x00090000,     // 20   20,17
    0x00140000,     // 21   21,19
    0x00300000,     // 22   22,21
    0x00420000,     // 23   23,18
    0x00e10000,     // 24   24,23,22,17
    0x01200000,     // 25   25,22      (this is enough for 8191x4095)
};

static unsigned int rndbits_y;
static unsigned int rndmask;

extern VL_Color curpal[256];

// Returns the number of bits needed to represent the given value
static int log2_ceil(uint32_t x)
{
    int n = 0;
    uint32_t v = 1;
    while(v < x)
    {
        n++;
        v <<= 1;
    }
    return n;
}

void VH_Startup()
{
    int rndbits_x = log2_ceil(screenWidth);
    rndbits_y = log2_ceil(screenHeight);

    int rndbits = rndbits_x + rndbits_y;
    if(rndbits < 17)
        rndbits = 17;       // no problem, just a bit slower
    else if(rndbits > 25)
        rndbits = 25;       // fizzle fade will not fill whole screen

    rndmask = rndmasks[rndbits - 17];
}

boolean FizzleFade (VL_Surface *source, int x1, int y1,
    unsigned width, unsigned height, unsigned frames, boolean abortable)
{
    VL_ScreenToScreen(source, screen);
    VH_UpdateScreen(); // Push the screen to the LCD!
    return false;
}
