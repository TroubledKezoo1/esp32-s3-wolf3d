#ifndef ESP32_IO_H
#define ESP32_IO_H

#include <Arduino.h>
#include <FS.h>
#include <SD.h>
#include <SPI.h>
#include <fcntl.h>
#include <sys/stat.h>
#define MAX_OPEN_FILES 16
extern File openFiles[MAX_OPEN_FILES];

#ifdef __cplusplus
extern "C" {
#endif

// Redirect POSIX standard IO to ESP32 IO
int esp32_open(const char *pathname, int flags, ...);
int esp32_read(int fd, void *buf, size_t count);
int esp32_write(int fd, const void *buf, size_t count);
off_t esp32_lseek(int fd, off_t offset, int whence);
int esp32_close(int fd);
int esp32_unlink(const char *pathname);
int esp32_stat(const char *pathname, struct stat *statbuf);

#ifdef __cplusplus
}
#endif

// Macros removed to avoid conflict with LovyanGFX File class
#define O_BINARY 0

#endif
