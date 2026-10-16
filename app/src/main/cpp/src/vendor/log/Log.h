//
// Created by SyXhOwN on 17/01/2025.
//

#pragma once

#include "../../main.h"

#include <android/log.h>

void Log(const char *fmt, ...);
void CrashLog(const char* fmt, ...);

#define MTA_DEBUG(...) __android_log_print(ANDROID_LOG_DEBUG, "DEBUG", "[DEBUG] " __VA_ARGS__)
#define DLOG(...) __android_log_print(ANDROID_LOG_DEBUG, "DEBUG", "[DEBUG] " __VA_ARGS__)