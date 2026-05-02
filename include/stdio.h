#ifndef _STDIO_H
#define _STDIO_H

#include <stddef.h>
#include <stdarg.h>

typedef struct __wasm_FILE FILE;

#define EOF (-1)
#define BUFSIZ 1024
#define FILENAME_MAX 256
#define L_tmpnam 32

#define _IOFBF 0
#define _IOLBF 1
#define _IONBF 2

#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2

FILE *__wasm_stdin(void);
FILE *__wasm_stdout(void);
FILE *__wasm_stderr(void);

#define stdin (__wasm_stdin())
#define stdout (__wasm_stdout())
#define stderr (__wasm_stderr())

int printf(const char *fmt, ...);
int vprintf(const char *fmt, va_list ap);
int fprintf(FILE *f, const char *fmt, ...);
int vfprintf(FILE *f, const char *fmt, va_list ap);
int sprintf(char *dst, const char *fmt, ...);
int snprintf(char *dst, size_t n, const char *fmt, ...);
int vsprintf(char *dst, const char *fmt, va_list ap);
int vsnprintf(char *dst, size_t n, const char *fmt, va_list ap);
int scanf(const char *fmt, ...);
int vscanf(const char *fmt, va_list ap);
int sscanf(const char *src, const char *fmt, ...);
int vsscanf(const char *src, const char *fmt, va_list ap);

int fputs(const char *s, FILE *f);
char *fgets(char *s, int n, FILE *f);
int puts(const char *s);
int putchar(int ch);
int getchar(void);
int fputc(int ch, FILE *f);
int fgetc(FILE *f);
#define getc(f) fgetc(f)
int ungetc(int ch, FILE *f);

size_t fread(void *ptr, size_t size, size_t nmemb, FILE *f);
size_t fwrite(const void *ptr, size_t size, size_t nmemb, FILE *f);
int fflush(FILE *f);
int fclose(FILE *f);
FILE *fopen(const char *path, const char *mode);
FILE *fdopen(int fd, const char *mode);
FILE *freopen(const char *path, const char *mode, FILE *f);
FILE *open_memstream(char **ptr, size_t *sizeloc);
FILE *tmpfile(void);
char *tmpnam(char *s);
int remove(const char *path);
int rename(const char *oldpath, const char *newpath);
int feof(FILE *f);
int ferror(FILE *f);
void clearerr(FILE *f);
int setvbuf(FILE *f, char *buf, int mode, size_t size);
int fscanf(FILE *f, const char *fmt, ...);
int fseek(FILE *f, long offset, int whence);
long ftell(FILE *f);

#endif
