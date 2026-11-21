#pragma once

#if defined(__ANDROID__)
#include <android/log.h>
#define FORJA_LOG(...) __android_log_print(ANDROID_LOG_INFO, "Forja", __VA_ARGS__)
#define FORJA_ERROR(...) __android_log_print(ANDROID_LOG_ERROR, "Forja", __VA_ARGS__)
#else
#include <cstdio>
#define FORJA_LOG(...) (std::fprintf(stdout, "[Forja] " __VA_ARGS__), std::fputc('\n', stdout), std::fflush(stdout))
#define FORJA_ERROR(...) (std::fprintf(stderr, "[Forja ERROR] " __VA_ARGS__), std::fputc('\n', stderr))
#endif
