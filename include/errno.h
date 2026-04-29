#ifndef _ERRNO_H
#define _ERRNO_H

#define EPERM   1
#define ENOENT  2
#define EIO     5
#define EBADF   9
#define ENOMEM 12
#define EINVAL 22
#define ENOSPC 28
#define ENOSYS 52

int *__errno_location(void);
#define errno (*__errno_location())

#endif
