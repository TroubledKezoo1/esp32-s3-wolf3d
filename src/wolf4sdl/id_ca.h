#ifndef __ID_CA__
#define __ID_CA__

//===========================================================================

#define NUMMAPS         60
#ifdef USE_FLOORCEILINGTEX
    #define MAPPLANES       3
#else
    #define MAPPLANES       2
#endif

#define UNCACHEGRCHUNK(chunk) {if(grsegs[chunk]) {free(grsegs[chunk]); grsegs[chunk]=NULL;}}
#define UNCACHEAUDIOCHUNK(chunk) {if(audiosegs[chunk]) {free(audiosegs[chunk]); audiosegs[chunk]=NULL;}}

//===========================================================================

typedef PACKED_STRUCT(
{
    int32_t planestart[3];
    uint16_t    planelength[3];
    uint16_t    width,height;
    char    name[16];
}) maptype;

//===========================================================================

extern  int   mapon;

extern  uint16_t *mapsegs[MAPPLANES];
extern  byte *audiosegs[NUMSNDCHUNKS];
extern  byte *grsegs[NUMCHUNKS];

extern  char  extension[5];
extern  char  graphext[5];
extern  char  audioext[5];

//===========================================================================

boolean CA_LoadFile (const char *filename, memptr *ptr);
boolean CA_WriteFile (const char *filename, void *ptr, int32_t length);

int32_t CA_RLEWCompress (uint16_t *source, int32_t length, uint16_t *dest, uint16_t rlewtag);

void CA_RLEWexpand (uint16_t *source, uint16_t *dest, int32_t length, uint16_t rlewtag, int32_t source_max_words);

void CA_Startup (void);
void CA_Shutdown (void);

int32_t CA_CacheAudioChunk (int chunk);
void CA_LoadAllSounds (void);

void CA_CacheGrChunk (int chunk);
void CA_CacheMap (int mapnum);

void CA_CacheScreen (int chunk);

void CA_CannotOpen(const char *name);

#endif
