#include "esp32_io.h"

File openFiles[MAX_OPEN_FILES];

int esp32_open(const char *pathname, int flags, ...) {
    int fd = -1;
    for (int i = 0; i < MAX_OPEN_FILES; i++) {
        if (!openFiles[i]) {
            fd = i;
            break;
        }
    }
    if (fd == -1) return -1; // No free file descriptors

    const char* mode = FILE_READ;
    if ((flags & O_CREAT) || (flags & O_WRONLY) || (flags & O_RDWR)) {
        mode = FILE_WRITE;
    }

    // SD.open requires absolute paths or specific formats, assuming filenames are in root "/"
    char fullpath[256];
    if (pathname[0] != '/') {
        snprintf(fullpath, sizeof(fullpath), "/%s", pathname);
    } else {
        snprintf(fullpath, sizeof(fullpath), "%s", pathname);
    }

    openFiles[fd] = SD.open(fullpath, mode);
    if (!openFiles[fd]) {
        // Retry with UPPERCASE filename just in case
        for (int i = 0; fullpath[i]; i++) {
            fullpath[i] = toupper(fullpath[i]);
        }
        openFiles[fd] = SD.open(fullpath, mode);
        
        if (!openFiles[fd]) {
            // Try lowercase as well
            for (int i = 0; fullpath[i]; i++) {
                fullpath[i] = tolower(fullpath[i]);
            }
            openFiles[fd] = SD.open(fullpath, mode);
            
            if (!openFiles[fd]) {
                return -1;
            }
        }
    }
    return fd + 3; // return a fd > 2 to avoid stdin/stdout/stderr clashes
}

int esp32_read(int fd, void *buf, size_t count) {
    static uint32_t last_yield = 0;
    uint32_t now = millis();
    if (now - last_yield > 100) {
        taskYIELD();
        last_yield = now;
    }
    fd -= 3;
    if (fd < 0 || fd >= MAX_OPEN_FILES || !openFiles[fd]) return -1;
    
    size_t total_read = 0;
    uint8_t *ptr = (uint8_t*)buf;
    while (total_read < count) {
        int bytes_read = openFiles[fd].read(ptr + total_read, count - total_read);
        if (bytes_read <= 0) {
            break; // EOF or error
        }
        total_read += bytes_read;
    }
    
    if (total_read == 0 && count > 0) return -1;
    return total_read;
}

int esp32_write(int fd, const void *buf, size_t count) {
    fd -= 3;
    if (fd < 0 || fd >= MAX_OPEN_FILES || !openFiles[fd]) return -1;
    
    size_t total_written = 0;
    const uint8_t *ptr = (const uint8_t*)buf;
    while (total_written < count) {
        int bytes_written = openFiles[fd].write(ptr + total_written, count - total_written);
        if (bytes_written <= 0) {
            break; // error
        }
        total_written += bytes_written;
    }
    
    if (total_written == 0 && count > 0) return -1;
    return total_written;
}

off_t esp32_lseek(int fd, off_t offset, int whence) {
    fd -= 3;
    if (fd < 0 || fd >= MAX_OPEN_FILES || !openFiles[fd]) return -1;

    off_t newPos = 0;
    if (whence == SEEK_SET) {
        newPos = offset;
    } else if (whence == SEEK_CUR) {
        newPos = openFiles[fd].position() + offset;
    } else if (whence == SEEK_END) {
        newPos = openFiles[fd].size() + offset;
    }

    if (openFiles[fd].seek(newPos)) {
        return newPos;
    }
    return -1;
}

int esp32_unlink(const char *pathname) {
    char fullpath[256];
    if (pathname[0] != '/') {
        snprintf(fullpath, sizeof(fullpath), "/%s", pathname);
    } else {
        snprintf(fullpath, sizeof(fullpath), "%s", pathname);
    }
    if (SD.remove(fullpath)) return 0;
    return -1;
}

int esp32_close(int fd) {
    fd -= 3;
    if (fd < 0 || fd >= MAX_OPEN_FILES || !openFiles[fd]) return -1;
    openFiles[fd].close();
    return 0;
}

int esp32_stat(const char *pathname, struct stat *statbuf) {
    if (!pathname) return -1;
    
    char fullpath[256];
    if (pathname[0] != '/') {
        snprintf(fullpath, sizeof(fullpath), "/%s", pathname);
    } else {
        snprintf(fullpath, sizeof(fullpath), "%s", pathname);
    }
    
    File f = SD.open(fullpath);
    if (!f) {
        for (int i = 0; fullpath[i]; i++) fullpath[i] = toupper(fullpath[i]);
        f = SD.open(fullpath);
        if (!f) {
            for (int i = 0; fullpath[i]; i++) fullpath[i] = tolower(fullpath[i]);
            f = SD.open(fullpath);
        }
    }
    
    if (f) {
        if (statbuf) {
            statbuf->st_size = f.size();
            statbuf->st_mode = S_IFREG | 0666;
            statbuf->st_mtime = 0;
            statbuf->st_atime = 0;
            statbuf->st_ctime = 0;
        }
        f.close();
        return 0;
    }
    return -1;
}
