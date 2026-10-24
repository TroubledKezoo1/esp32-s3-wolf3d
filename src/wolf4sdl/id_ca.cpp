// ID_CA.C

// this has been customized for WOLF

/*
=============================================================================

Id Software Caching Manager
---------------------------

Must be started BEFORE the memory manager, because it needs to get the headers
loaded into the data segment

=============================================================================
*/

#include <sys/types.h>
#if defined _WIN32
    #include <io.h>
#else
    #include <sys/uio.h>
    #include <unistd.h>
#endif

#include "wl_def.h"

#define THREEBYTEGRSTARTS

/*
=============================================================================

                             LOCAL CONSTANTS

=============================================================================
*/

typedef PACKED_STRUCT(
{
    uint16_t bit0,bit1;       // 0-255 is a character, > is a pointer to a node
}) huffnode;


typedef PACKED_STRUCT(
{
    uint16_t RLEWtag;
    int32_t headeroffsets[100];
}) mapfiletype;


/*
=============================================================================

                             GLOBAL VARIABLES

=============================================================================
*/

#define BUFFERSIZE 0x1000
static int32_t bufferseg[BUFFERSIZE/4];

int     mapon;

uint16_t    *mapsegs[MAPPLANES];
static maptype* mapheaderseg[NUMMAPS];
byte    *audiosegs[NUMSNDCHUNKS];
byte    *grsegs[NUMCHUNKS];

uint16_t    RLEWtag;

int     numEpisodesMissing = 0;

/*
=============================================================================

                             LOCAL VARIABLES

=============================================================================
*/

char extension[5]; // Need a string, not constant to change cache files
char graphext[5];
char audioext[5];
static const char gheadname[] = "vgahead.";
static const char gfilename[] = "vgagraph.";
static const char gdictname[] = "vgadict.";
static const char mheadname[] = "maphead.";
static const char aheadname[] = "audiohed.";
static const char afilename[] = "audiot.";

void CA_CannotOpen(const char *string);

static int32_t  grstarts[NUMCHUNKS + 1];
static int32_t* audiostarts; // array of offsets in audio / audiot

#ifdef GRHEADERLINKED
huffnode *grhuffman;
#else
huffnode grhuffman[255];
#endif

int    grhandle = -1;               // handle to EGAGRAPH
int    maphandle = -1;              // handle to MAPTEMP / GAMEMAPS
int    audiohandle = -1;            // handle to AUDIOT / AUDIO

int32_t   chunkcomplen,chunkexplen;

SDMode oldsoundmode;


static int32_t GRFILEPOS(const size_t idx)
{
	assert(idx < lengthof(grstarts));
	return grstarts[idx];
}

/*
=============================================================================

                            LOW LEVEL ROUTINES

=============================================================================
*/

/*
============================
=
= CAL_GetGrChunkLength
=
= Gets the length of an explicit length chunk (not tiles)
= The file pointer is positioned so the compressed data can be read in next.
=
============================
*/

void CAL_GetGrChunkLength (int chunk)
{
    esp32_lseek(grhandle,GRFILEPOS(chunk),SEEK_SET);
    if (esp32_read(grhandle,&chunkexplen,sizeof(chunkexplen)) < 0)
    {
        return;
    }
    chunkcomplen = GRFILEPOS(chunk+1)-GRFILEPOS(chunk)-4;
}


/*
==========================
=
= CA_WriteFile
=
= Writes a file from a memory buffer
=
==========================
*/

boolean CA_WriteFile (const char *filename, void *ptr, int32_t length)
{
    const int handle = esp32_open(filename, O_CREAT | O_WRONLY | O_BINARY, 0644);
    if (handle == -1)
        return false;

    if (!esp32_write(handle,ptr,length))
    {
        esp32_close(handle);
        return false;
    }
    esp32_close(handle);
    return true;
}



/*
==========================
=
= CA_LoadFile
=
= Allocate space for and load a file
=
==========================
*/

boolean CA_LoadFile (const char *filename, memptr *ptr)
{
    int32_t size;

    const int handle = esp32_open(filename, O_RDONLY | O_BINARY);
    if (handle == -1)
        return false;

    size = esp32_lseek(handle, 0, SEEK_END);
    esp32_lseek(handle, 0, SEEK_SET);
    *ptr = heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!*ptr) *ptr = malloc(size);
    CHECKMALLOCRESULT(*ptr);
    if (!esp32_read(handle,*ptr,size))
    {
        esp32_close(handle);
        return false;
    }
    esp32_close(handle);
    return true;
}

/*
============================================================================

                COMPRESSION routines, see JHUFF.C for more

============================================================================
*/

static void CAL_HuffExpand(byte *source, byte *dest, int32_t length, huffnode *hufftable)
{
    byte *end;
    huffnode *headptr, *huffptr;

    if(!length || !dest)
    {
        Quit("length or dest is null!");
        return;
    }

    headptr = hufftable+254;        // head node is always node 254

    end=dest+length;

    byte val = *source++;
    byte mask = 1;
    uint16_t nodeval;
    huffptr = headptr;
    while(1)
    {
        if(!(val & mask))
            nodeval = huffptr->bit0;
        else
            nodeval = huffptr->bit1;
        if(mask==0x80)
        {
            val = *source++;
            mask = 1;
        }
        else mask <<= 1;

        if(nodeval<256)
        {
            *dest++ = (byte) nodeval;
            huffptr = headptr;
            if(dest>=end) break;
        }
        else
        {
            huffptr = hufftable + (nodeval - 256);
        }
    }
}

/*
======================
=
= CAL_CarmackExpand
=
= Length is the length of the EXPANDED data
=
======================
*/

#define NEARTAG 0xa7
#define FARTAG  0xa8

void CAL_CarmackExpand (byte *source, uint16_t *dest, int length)
{
    uint16_t ch,chhigh,count,offset;
    byte *inptr;
    uint16_t *copyptr, *outptr;

    length/=2;

    inptr = (byte *) source;
    outptr = dest;

    while (length>0)
    {
        ch = READWORD(inptr);
        chhigh = ch>>8;
        if (chhigh == NEARTAG)
        {
            count = ch&0xff;
            if (!count)
            {                               // have to insert a uint16_t containing the tag byte
                ch |= *inptr++;
                *outptr++ = ch;
                length--;
            }
            else
            {
                offset = *inptr++;
                copyptr = outptr - offset;
                length -= count;
                if(length<0) return;
                while (count--)
                    *outptr++ = *copyptr++;
            }
        }
        else if (chhigh == FARTAG)
        {
            count = ch&0xff;
            if (!count)
            {                               // have to insert a uint16_t containing the tag byte
                ch |= *inptr++;
                *outptr++ = ch;
                length --;
            }
            else
            {
                offset = READWORD(inptr);
                copyptr = dest + offset;
                length -= count;
                if(length<0) return;
                while (count--)
                    *outptr++ = *copyptr++;
            }
        }
        else
        {
            *outptr++ = ch;
            length --;
        }
    }
}

/*
======================
=
= CA_RLEWcompress
=
======================
*/

int32_t CA_RLEWCompress (uint16_t *source, int32_t length, uint16_t *dest, uint16_t rlewtag)
{
    uint16_t value,count;
    unsigned i;
    uint16_t *start,*end;

    start = dest;

    end = source + (length+1)/2;

    //
    // compress it
    //
    do
    {
        count = 1;
        value = *source++;
        while (*source == value && source<end)
        {
            count++;
            source++;
        }
        if (count>3 || value == rlewtag)
        {
            //
            // send a tag / count / value string
            //
            *dest++ = rlewtag;
            *dest++ = count;
            *dest++ = value;
        }
        else
        {
            //
            // send uint16_t without compressing
            //
            for (i=1;i<=count;i++)
                *dest++ = value;
        }

    } while (source<end);

    return (int32_t)(2*(dest-start));
}


/*
======================
=
= CA_RLEWexpand
= length is EXPANDED length
=
======================
*/

void CA_RLEWexpand (uint16_t *source, uint16_t *dest, int32_t length, uint16_t rlewtag, int32_t source_max_words)
{
    uint16_t value,count,i;
    uint16_t *end=dest+length/2;
    uint16_t *source_end = source + source_max_words;

    while (dest < end && source < source_end)
    {
        value = *source++;
        if (value != rlewtag)
        {
            *dest++ = value;
        }
        else
        {
            if (source >= source_end) break;
            count = *source++;
            if (source >= source_end) break;
            value = *source++;
            
            while (count > 0 && dest < end)
            {
                *dest++ = value;
                count--;
            }
        }
    }
}



/*
=============================================================================

                                         CACHE MANAGER ROUTINES

=============================================================================
*/


/*
======================
=
= CAL_SetupGrFile
=
======================
*/

void CAL_SetupGrFile (void)
{
    char fname[13];
    int handle;
    byte *compseg;

#ifdef GRHEADERLINKED

    grhuffman = (huffnode *)&EGAdict;
    grstarts = (int32_t _seg *)FP_SEG(&EGAhead);

#else

//
// load ???dict.ext (huffman dictionary for graphics files)
//

    strcpy(fname,gdictname);
    strcat(fname,graphext);

    handle = esp32_open(fname, O_RDONLY | O_BINARY);
    if (handle == -1)
        CA_CannotOpen(fname);

    if (esp32_read(handle, grhuffman, sizeof(grhuffman)) < 0)
    {
        esp32_close(handle);
        return;
    }
    esp32_close(handle);

    // load the data offsets from ???head.ext
    strcpy(fname,gheadname);
    strcat(fname,graphext);

    handle = esp32_open(fname, O_RDONLY | O_BINARY);
    if (handle == -1)
        CA_CannotOpen(fname);

    long headersize = esp32_lseek(handle, 0, SEEK_END);
    esp32_lseek(handle, 0, SEEK_SET);

#ifndef APOGEE_1_0
	int expectedsize = lengthof(grstarts) - numEpisodesMissing;
#else
	int expectedsize = lengthof(grstarts);
#endif

    if(!param_ignorenumchunks && headersize / 3 != (long) expectedsize)
    {
        // Just print a warning instead of quitting
        Serial.printf("WARNING: vgahead has %ld offsets, expected %d\n", headersize/3, expectedsize);
    }

    int num_offsets = headersize / 3;
    if (num_offsets > expectedsize) num_offsets = expectedsize;

    byte data[num_offsets * 3];
    if (esp32_read(handle, data, sizeof(data)) < 0)
    {
        esp32_close(handle);
        return;
    }
    esp32_close(handle);

    const byte* d = data;
    int32_t* ptr = grstarts;
    for (int count = 0; count < num_offsets && ptr != endof(grstarts); ++count, ++ptr)
    {
        const int32_t val = d[0] | d[1] << 8 | d[2] << 16;
        *ptr = (val == 0x00FFFFFF ? -1 : val);
        d += 3;
    }
    // Fill the rest with -1
    while (ptr != endof(grstarts))
    {
        *ptr++ = -1;
    }
#endif

//
// Open the graphics file, leaving it open until the game is finished
//
    strcpy(fname,gfilename);
    strcat(fname,graphext);

    grhandle = esp32_open(fname, O_RDONLY | O_BINARY);
    if (grhandle == -1)
        CA_CannotOpen(fname);


//
// load the pic and sprite headers into the arrays in the data segment
//
    pictable=(pictabletype *) malloc(NUMPICS*sizeof(pictabletype));
    CHECKMALLOCRESULT(pictable);
    CAL_GetGrChunkLength(STRUCTPIC);                // position file pointer
    
    compseg = (byte *) heap_caps_malloc(chunkcomplen, MALLOC_CAP_SPIRAM);
    if (!compseg) compseg = (byte *) malloc(chunkcomplen);
    CHECKMALLOCRESULT(compseg);
    if (esp32_read(grhandle,compseg,chunkcomplen) < 0)
    {
        free(compseg);
        return;
    }
    CAL_HuffExpand(compseg, (byte*)pictable, NUMPICS * sizeof(pictabletype), grhuffman);
    free(compseg);
}

//==========================================================================


/*
======================
=
= CAL_SetupMapFile
=
======================
*/

void CAL_SetupMapFile (void)
{
    int     i;
    int handle;
    int32_t length,pos;
    char fname[13];

//
// load maphead.ext (offsets and tileinfo for map file)
//
    strcpy(fname,mheadname);
    strcat(fname,extension);

    handle = esp32_open(fname, O_RDONLY | O_BINARY);
    if (handle == -1)
        CA_CannotOpen(fname);

    length = NUMMAPS*4+2; // used to be "esp32_filelength(handle);"
    mapfiletype *tinf=(mapfiletype *) malloc(sizeof(mapfiletype));
    CHECKMALLOCRESULT(tinf);
    if (esp32_read(handle, tinf, length) < 0)
    {
        esp32_close(handle);
        free(tinf);
        return;
    }
    esp32_close(handle);

    RLEWtag=tinf->RLEWtag;

//
// open the data file
//
#ifdef CARMACIZED
    strcpy(fname, "gamemaps.");
    strcat(fname, extension);

    maphandle = esp32_open(fname, O_RDONLY | O_BINARY);
    if (maphandle == -1)
        CA_CannotOpen(fname);
#else
    strcpy(fname,"maptemp.");
    strcat(fname,extension);

    maphandle = esp32_open(fname, O_RDONLY | O_BINARY);
    if (maphandle == -1)
        CA_CannotOpen(fname);
#endif

//
// load all map header
//
    for (i=0;i<NUMMAPS;i++)
    {
        pos = tinf->headeroffsets[i];
        if (pos<0)                          // $FFFFFFFF start is a sparse map
            continue;

        mapheaderseg[i]=(maptype *) malloc(sizeof(maptype));
        CHECKMALLOCRESULT(mapheaderseg[i]);
        esp32_lseek(maphandle,pos,SEEK_SET);
        if (esp32_read(maphandle,(memptr)mapheaderseg[i],sizeof(maptype)) < 0)
        {
            free(tinf);
            return;
        }
    }

    free(tinf);

//
// allocate space for 3 64*64 planes
//
#include <esp_heap_caps.h>
    for (i=0;i<MAPPLANES;i++)
    {
        if (!mapsegs[i]) {
            mapsegs[i] = (uint16_t*)heap_caps_malloc(4096 * sizeof(uint16_t), MALLOC_CAP_SPIRAM);
        }
    }
}


//==========================================================================


/*
======================
=
= CAL_SetupAudioFile
=
======================
*/

void CAL_SetupAudioFile (void)
{
    char fname[13];

//
// load audiohed.ext (offsets for audio file)
//
    strcpy(fname,aheadname);
    strcat(fname,audioext);

    void* ptr;
    if (!CA_LoadFile(fname, &ptr))
        CA_CannotOpen(fname);
    audiostarts = (int32_t*)ptr;

//
// open the data file
//
    strcpy(fname,afilename);
    strcat(fname,audioext);

    audiohandle = esp32_open(fname, O_RDONLY | O_BINARY);
    if (audiohandle == -1)
        CA_CannotOpen(fname);
}

//==========================================================================


/*
======================
=
= CA_Startup
=
= Open all files and load in headers
=
======================
*/

void CA_Startup (void)
{
#ifdef PROFILE
    esp32_unlink("PROFILE.TXT");
    profilehandle = esp32_open("PROFILE.TXT", O_CREAT | O_WRONLY | O_TEXT);
#endif

    CAL_SetupMapFile ();
    CAL_SetupGrFile ();
    CAL_SetupAudioFile ();

    mapon = -1;
}

//==========================================================================


/*
======================
=
= CA_Shutdown
=
= Closes all files
=
======================
*/

void CA_Shutdown (void)
{
    int i,start = STARTPCSOUNDS;

    if(maphandle != -1)
        esp32_close(maphandle);
    if(grhandle != -1)
        esp32_close(grhandle);
    if(audiohandle != -1)
        esp32_close(audiohandle);

    for(i=0; i<NUMCHUNKS; i++)
        UNCACHEGRCHUNK(i);
    free(pictable);

    switch(oldsoundmode)
    {
        case sdm_Off:
            return;
        case sdm_PC:
            start = STARTPCSOUNDS;
            break;
        case sdm_AdLib:
            start = STARTADLIBSOUNDS;
            break;
    }

    for(i=0; i<NUMSOUNDS; i++,start++)
        UNCACHEAUDIOCHUNK(start);
}

//===========================================================================

/*
======================
=
= CA_CacheAudioChunk
=
======================
*/

int32_t CA_CacheAudioChunk (int chunk)
{
    int32_t pos = audiostarts[chunk];
    int32_t size = audiostarts[chunk+1]-pos;

    if (audiosegs[chunk])
        return size;                        // already in memory

    audiosegs[chunk] = (byte*)heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!audiosegs[chunk]) audiosegs[chunk] = (byte *) malloc(size);
    CHECKMALLOCRESULT(audiosegs[chunk]);

    esp32_lseek(audiohandle,pos,SEEK_SET);
    if (esp32_read(audiohandle,audiosegs[chunk],size) < 0)
    {
        free(audiosegs[chunk]);
        audiosegs[chunk] = NULL;
        return 0;
    }

    return size;
}

void CA_CacheAdlibSoundChunk (int chunk)
{
    int32_t pos = audiostarts[chunk];
    int32_t size = audiostarts[chunk+1]-pos;

    if (audiosegs[chunk])
        return;                        // already in memory

    esp32_lseek(audiohandle, pos, SEEK_SET);
    if (esp32_read(audiohandle, bufferseg, ORIG_ADLIBSOUND_SIZE - 1) < 0) // without data[1]
    {
        return;
    }

    audiosegs[chunk] = (byte*)heap_caps_malloc(size + sizeof(AdLibSound) - ORIG_ADLIBSOUND_SIZE, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!audiosegs[chunk]) audiosegs[chunk] = (byte *) malloc(size + sizeof(AdLibSound) - ORIG_ADLIBSOUND_SIZE);
    CHECKMALLOCRESULT(audiosegs[chunk]);
    void *p = audiosegs[chunk];
    AdLibSound *sound = (AdLibSound *) p;

    byte *ptr = (byte *) bufferseg;
    sound->common.length = READLONGWORD(ptr);
    sound->common.priority = READWORD(ptr);
    sound->inst.mChar = *ptr++;
    sound->inst.cChar = *ptr++;
    sound->inst.mScale = *ptr++;
    sound->inst.cScale = *ptr++;
    sound->inst.mAttack = *ptr++;
    sound->inst.cAttack = *ptr++;
    sound->inst.mSus = *ptr++;
    sound->inst.cSus = *ptr++;
    sound->inst.mWave = *ptr++;
    sound->inst.cWave = *ptr++;
    sound->inst.nConn = *ptr++;
    sound->inst.voice = *ptr++;
    sound->inst.mode = *ptr++;
    sound->inst.unused[0] = *ptr++;
    sound->inst.unused[1] = *ptr++;
    sound->inst.unused[2] = *ptr++;
    sound->block = *ptr++;

    if (esp32_read(audiohandle, sound->data, size - ORIG_ADLIBSOUND_SIZE + 1) < 0) // + 1 because of byte data[1]
    {
        free(audiosegs[chunk]);
        audiosegs[chunk] = NULL;
        return;
    }

    audiosegs[chunk]=(byte *) sound;
}

//===========================================================================

/*
======================
=
= CA_LoadAllSounds
=
= Purges all sounds, then loads all new ones (mode switch)
=
======================
*/

void CA_LoadAllSounds (void)
{
    unsigned start = STARTPCSOUNDS,i;

    switch (oldsoundmode)
    {
        case sdm_Off:
            goto cachein;
        case sdm_PC:
            start = STARTPCSOUNDS;
            break;
        case sdm_AdLib:
            start = STARTADLIBSOUNDS;
            break;
    }

    for (i=0;i<NUMSOUNDS;i++,start++)
        UNCACHEAUDIOCHUNK(start);

cachein:

    oldsoundmode = SoundMode;

    switch (SoundMode)
    {
        case sdm_Off:
            start = STARTADLIBSOUNDS;   // needed for priorities...
            break;
        case sdm_PC:
            start = STARTPCSOUNDS;
            break;
        case sdm_AdLib:
            start = STARTADLIBSOUNDS;
            break;
    }

    if(start == STARTADLIBSOUNDS)
    {
        for (i=0;i<NUMSOUNDS;i++,start++)
            CA_CacheAdlibSoundChunk(start);
    }
    else
    {
        for (i=0;i<NUMSOUNDS;i++,start++)
            CA_CacheAudioChunk(start);
    }
}

//===========================================================================


/*
======================
=
= CAL_ExpandGrChunk
=
= Does whatever is needed with a pointer to a compressed chunk
=
======================
*/

void CAL_ExpandGrChunk (int chunk, int32_t *source)
{
    int32_t    expanded;

    if (chunk >= STARTTILE8 && chunk < STARTEXTERNS)
    {
        //
        // expanded sizes of tile8/16/32 are implicit
        //

#define BLOCK           64
#define MASKBLOCK       128

        if (chunk<STARTTILE8M)          // tile 8s are all in one chunk!
            expanded = BLOCK*NUMTILE8;
        else if (chunk<STARTTILE16)
            expanded = MASKBLOCK*NUMTILE8M;
        else if (chunk<STARTTILE16M)    // all other tiles are one/chunk
            expanded = BLOCK*4;
        else if (chunk<STARTTILE32)
            expanded = MASKBLOCK*4;
        else if (chunk<STARTTILE32M)
            expanded = BLOCK*16;
        else
            expanded = MASKBLOCK*16;
    }
    else
    {
        //
        // everything else has an explicit size longword
        //
        expanded = *source++;
    }

    //
    // allocate final space, decompress it, and free bigbuffer
    // Sprites need to have shifts made and various other junk
    //
    grsegs[chunk] = (byte*)heap_caps_malloc(expanded, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!grsegs[chunk]) grsegs[chunk] = (byte *) malloc(expanded);
    CHECKMALLOCRESULT(grsegs[chunk]);
    CAL_HuffExpand((byte *) source, grsegs[chunk], expanded, grhuffman);
}


/*
======================
=
= CA_CacheGrChunk
=
= Makes sure a given chunk is in memory, loadiing it if needed
=
======================
*/

void CA_CacheGrChunk (int chunk)
{
    int32_t pos,compressed;
    int32_t *source;
    int  next;

    if (grsegs[chunk])
        return;                             // already in memory

//
// load the chunk into a buffer, either the miscbuffer if it fits, or allocate
// a larger buffer
//
    pos = GRFILEPOS(chunk);
    if (pos<0)                              // $FFFFFFFF start is a sparse tile
        return;

    next = chunk +1;
    while (GRFILEPOS(next) == -1)           // skip past any sparse tiles
        next++;

    compressed = GRFILEPOS(next)-pos;

    esp32_lseek(grhandle,pos,SEEK_SET);

    if (compressed<=BUFFERSIZE)
    {
        if (esp32_read(grhandle,bufferseg,compressed) < 0)
        {
            return;
        }
        source = bufferseg;
    }
    else
    {
        source = (int32_t *) heap_caps_malloc(compressed, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (!source) source = (int32_t *) malloc(compressed);
        CHECKMALLOCRESULT(source);
        if (esp32_read(grhandle,source,compressed) < 0)
        {
            free(source);
            return;
        }
    }

    CAL_ExpandGrChunk (chunk,source);

    if (compressed>BUFFERSIZE)
        free(source);
}



//==========================================================================

/*
======================
=
= CA_CacheScreen
=
= Decompresses a chunk from disk straight onto the screen
=
======================
*/

void CA_CacheScreen (int chunk)
{
    int32_t    pos,compressed,expanded;
    memptr  bigbufferseg;
    int32_t    *source;
    int             next;
    byte *pic, *vbuf;
    int x, y, scx, scy;
    unsigned i, j;

//
// load the chunk into a buffer
//
    pos = GRFILEPOS(chunk);
    next = chunk +1;
    while (GRFILEPOS(next) == -1)           // skip past any sparse tiles
        next++;
    compressed = GRFILEPOS(next)-pos;

    esp32_lseek(grhandle,pos,SEEK_SET);

    bigbufferseg = heap_caps_malloc(compressed, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!bigbufferseg) bigbufferseg = malloc(compressed);
    CHECKMALLOCRESULT(bigbufferseg);
    if (esp32_read(grhandle,bigbufferseg,compressed) < 0)
    {
        free(bigbufferseg);
        return;
    }
    source = (int32_t *) bigbufferseg;

    expanded = *source++;

//
// allocate final space, decompress it, and free bigbuffer
// Sprites need to have shifts made and various other junk
//
    pic = (byte *) heap_caps_malloc(64000, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!pic) pic = (byte *) malloc(64000);
    CHECKMALLOCRESULT(pic);
    CAL_HuffExpand((byte *) source, pic, expanded, grhuffman);

    vbuf = VL_LockSurface(curSurface);
    if(vbuf != NULL)
    {
        for(y = 0, scy = 0; y < 200; y++, scy += scaleFactor)
        {
            for(x = 0, scx = 0; x < 320; x++, scx += scaleFactor)
            {
                byte col = pic[(y * 80 + (x >> 2)) + (x & 3) * 80 * 200];
                for(i = 0; i < scaleFactor; i++)
                    for(j = 0; j < scaleFactor; j++)
                        vbuf[(scy + i) * curPitch + scx + j] = col;
            }
        }
        VL_UnlockSurface(curSurface);
    }
    free(pic);
    free(bigbufferseg);
}

//==========================================================================

/*
======================
=
= CA_CacheMap
=
= WOLF: This is specialized for a 64*64 map size
=
======================
*/


void CA_CacheMap (int mapnum)
{
    int32_t   pos,compressed;
    int       plane;
    uint16_t     *dest;
    unsigned  size;
    uint16_t     *source;
#ifdef CARMACIZED
    uint16_t     expanded;
#endif

    // Static buffers to prevent heap corruption and free() crashes!
    static uint16_t static_buffer2seg[4096]; 
    static uint8_t  static_bigbufferseg[16384];

    mapnum &= ~0x80;    // skip the secret level bit

    for (plane = 0; plane<MAPPLANES; plane++)
    {
        pos = mapheaderseg[mapnum]->planestart[plane];
        compressed = mapheaderseg[mapnum]->planelength[plane];

        dest = mapsegs[plane];
        size = maparea*2;

        if (!pos)
        {
            //
            // empty plane (corrupt map?)
            //
            memset(dest,0,size);
            continue;
        }

        esp32_lseek(maphandle,pos,SEEK_SET);
        if (compressed<=BUFFERSIZE)
            source = (uint16_t *) bufferseg;
        else
        {
            source = (uint16_t *) static_bigbufferseg;
        }

        if (esp32_read(maphandle,source,compressed) != compressed)
        {
            delay(2000);
            return;
        }

#ifdef CARMACIZED
        //
        // unhuffman, then unRLEW
        // The huffman'd chunk has a two byte expanded length first
        // The resulting RLEW chunk also does, even though it's not really
        // needed
        //
        expanded = *source;
        source++;
        char dbg[64];
        sprintf(dbg, "exp: %d, comp: %d", (int)expanded, (int)compressed);

        uint16_t *buffer2seg = static_buffer2seg;
        
        CAL_CarmackExpand((byte *) source, buffer2seg,expanded);
        CA_RLEWexpand(buffer2seg+1,dest,size,RLEWtag,(expanded/2)-1);
#else
        //
        // unRLEW, skipping expanded length
        //
        CA_RLEWexpand (source+1,dest,size,RLEWtag, (compressed/2)-1);
#endif
    }
}

//===========================================================================

void CA_CannotOpen(const char *string)
{
    char str[30];

    strcpy(str,"Can't open ");
    strcat(str,string);
    strcat(str,"!\n");
    Quit (str);
}
