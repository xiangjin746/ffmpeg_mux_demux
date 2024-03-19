#ifndef __LOG_H
#define __LOG_H

#include <stdio.h>
#include <string.h>
#include <time.h>

//#define LOG(format, args...) KHJUtilLog(__FILE__, __FUNCTION__, __LINE__, format, ## args)
#define LOG(format, args...)

static inline const char *getFileName(const char *file)
{
    char * pos = strrchr(file, '/');
    return pos ? pos + 1 : file;
}

static inline void KHJUtilLog(const char *file, const char *tag, int line, const char *fmt, ...)
{
//    char buffer_fmt[1024] = {0};
//    struct timespec spec;
//    struct tm ptm;
//    va_list ap;


//    memset(&ptm, 0, sizeof(ptm));

//    spec.tv_nsec = 0;
//    spec.tv_sec = time(NULL);

//    localtime(&spec.tv_sec);

//    snprintf(buffer_fmt, sizeof(buffer_fmt) - 1, "[%4d-%02d-%02d %02d:%02d:%02d][%s:%s:%-6d]: ",
//             ptm.tm_year + 1900, ptm.tm_mon + 1, ptm.tm_mday, ptm.tm_hour, ptm.tm_min, ptm.tm_sec,
//             getFileName(file), tag, line);


//    strncat(buffer_fmt, fmt, sizeof(buffer_fmt) - strlen(buffer_fmt) - 1);

//    va_start(ap, fmt);


//    vprintf(buffer_fmt, ap);

//#if defined(_WIN32)
//    fflush(stdout);
//#endif

//    va_end(ap);

}

#endif //__LOG_H
