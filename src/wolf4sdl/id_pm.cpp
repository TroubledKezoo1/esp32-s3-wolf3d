#include "wl_def.h"
#include "../esp32_io.h"
#include <esp_heap_caps.h>

int ChunksInFile;
int PMSpriteStart;
int PMSoundStart;

bool PMSoundInfoPagePadded = false;

// holds the whole VSWAP
uint32_t *PMPageData;
size_t PMPageDataSize;

// ChunksInFile+1 pointers to page starts.
// The last pointer points one byte after the last page.
uint8_t **PMPages;

void PM_Startup()
{
    char fname[13] = "vswap.";
    strcat(fname,extension);

    int fd = esp32_open(fname, O_RDONLY | O_BINARY);
    if(fd == -1)
        CA_CannotOpen(fname);

    uint16_t temp16;

    if (esp32_read(fd, &temp16, 2) != 2)
    {
        esp32_close(fd);
        return;
    }
    ChunksInFile = temp16;
    
    if (esp32_read(fd, &temp16, 2) != 2)
    {
        esp32_close(fd);
        return;
    }
    PMSpriteStart = temp16;

    if (esp32_read(fd, &temp16, 2) != 2)
    {
        esp32_close(fd);
        return;
    }
    PMSoundStart = temp16;

    uint32_t* pageOffsets = (uint32_t *) heap_caps_malloc((ChunksInFile + 1) * sizeof(int32_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!pageOffsets) pageOffsets = (uint32_t *) malloc((ChunksInFile + 1) * sizeof(int32_t));
    CHECKMALLOCRESULT(pageOffsets);
    if (esp32_read(fd, pageOffsets, sizeof(uint32_t) * ChunksInFile) != (int)(sizeof(uint32_t) * ChunksInFile))
    {
        free(pageOffsets);
        esp32_close(fd);
        return;
    }

    uint16_t *pageLengths = (uint16_t *) heap_caps_malloc(ChunksInFile * sizeof(uint16_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!pageLengths) pageLengths = (uint16_t *) malloc(ChunksInFile * sizeof(uint16_t));
    CHECKMALLOCRESULT(pageLengths);
    if (esp32_read(fd, pageLengths, sizeof(uint16_t) * ChunksInFile) != (int)(sizeof(uint16_t) * ChunksInFile))
    {
        free(pageLengths);
        free(pageOffsets);
        esp32_close(fd);
        return;
    }

    long fileSize = esp32_lseek(fd, 0, SEEK_END);
    esp32_lseek(fd, 0, SEEK_SET);

    size_t pageDataSize = fileSize - pageOffsets[0];
    if(pageDataSize > (size_t) -1)
        Quit("The page file \"%s\" is too large!", fname);

    pageOffsets[ChunksInFile] = fileSize;

    uint32_t dataStart = pageOffsets[0];
    int i;

    // Check that all pageOffsets are valid
    for(i = 0; i < ChunksInFile; i++)
    {
        if(!pageOffsets[i]) continue;   // sparse page
        if(pageOffsets[i] < dataStart || pageOffsets[i] >= (size_t) fileSize)
            Quit("Illegal page offset for page %i: %u (filesize: %u)",
                    i, pageOffsets[i], fileSize);
    }

    // Calculate total amount of padding needed for sprites and sound info page
    int alignPadding = 0;
    for(i = PMSpriteStart; i < PMSoundStart; i++)
    {
        if(!pageOffsets[i]) continue;   // sparse page
        uint32_t offs = pageOffsets[i] - dataStart + alignPadding;
        if(offs & 1)
            alignPadding++;
    }

    if((pageOffsets[ChunksInFile - 1] - dataStart + alignPadding) & 1)
        alignPadding++;

    PMPageDataSize = (size_t) pageDataSize + alignPadding;
    PMPageData = (uint32_t *) heap_caps_malloc(PMPageDataSize, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!PMPageData) PMPageData = (uint32_t *) malloc(PMPageDataSize);
    CHECKMALLOCRESULT(PMPageData);

    PMPages = (uint8_t **) heap_caps_malloc((ChunksInFile + 1) * sizeof(uint8_t *), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!PMPages) PMPages = (uint8_t **) malloc((ChunksInFile + 1) * sizeof(uint8_t *));
    CHECKMALLOCRESULT(PMPages);

    // Load pages and initialize PMPages pointers
    uint8_t *ptr = (uint8_t *) PMPageData;
    for(i = 0; i < ChunksInFile; i++)
    {
        if((i >= PMSpriteStart && i < PMSoundStart) || i == ChunksInFile - 1)
        {
            size_t offs = ptr - (uint8_t *) PMPageData;

            // pad with zeros to make it 2-byte aligned
            if(offs & 1)
            {
                *ptr++ = 0;
                if(i == ChunksInFile - 1) PMSoundInfoPagePadded = true;
            }
        }

        PMPages[i] = ptr;

        if(!pageOffsets[i])
            continue;               // sparse page

        // Use specified page length, when next page is sparse page.
        // Otherwise, calculate size from the offset difference between this and the next page.
        uint32_t size;
        if(!pageOffsets[i + 1]) size = pageLengths[i];
        else size = pageOffsets[i + 1] - pageOffsets[i];

        esp32_lseek(fd, pageOffsets[i], SEEK_SET);
        if (esp32_read(fd, ptr, size) != (int)size)
        {
            free(pageLengths);
            free(pageOffsets);
            esp32_close(fd);
            return;
        }
        ptr += size;
    }

    // last page points after page buffer
    PMPages[ChunksInFile] = ptr;

    free(pageLengths);
    free(pageOffsets);
    esp32_close(fd);
}

void PM_Shutdown()
{
    free(PMPages);
    free(PMPageData);
}
