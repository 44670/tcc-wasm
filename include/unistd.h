#ifndef _UNISTD_H
#define _UNISTD_H

#include <stddef.h>

#ifndef __TCC_SSIZE_T_TYPEDEF
#define __TCC_SSIZE_T_TYPEDEF
typedef int ssize_t;
#endif

#ifndef __TCC_OFF_T_TYPEDEF
#define __TCC_OFF_T_TYPEDEF
typedef int off_t;
#endif

int close(int fd);
ssize_t read(int fd, void *buf, size_t count);
ssize_t write(int fd, const void *buf, size_t count);
off_t lseek(int fd, off_t offset, int whence);
int unlink(const char *path);
char *getcwd(char *buf, size_t size);
int isatty(int fd);

#endif
