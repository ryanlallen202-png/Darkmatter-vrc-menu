#pragma once
// Host syntax-check stub — never shipped into the arm64 build.
#include <stdio.h>
#include <stdarg.h>
enum { ANDROID_LOG_INFO = 4, ANDROID_LOG_ERROR = 6, ANDROID_LOG_WARN = 5, ANDROID_LOG_DEBUG = 3 };
static inline int __android_log_print(int, const char* tag, const char* fmt, ...) {
    va_list ap; va_start(ap, fmt); vfprintf(stderr, fmt, ap); va_end(ap); (void)tag; return 0;
}
