//
// Created by edz on 2020/7/29.
//

#ifndef NATIVE_ANDROIDLOG_H
#define NATIVE_ANDROIDLOG_H

#include "android/log.h"
#define LOGD(FORMAT,...) __android_log_print(ANDROID_LOG_DEBUG,"native_D",FORMAT,##__VA_ARGS__);

#define LOGE(FORMAT, ...) __android_log_print(ANDROID_LOG_WARN, "native_E", FORMAT, ##__VA_ARGS__);

#define LOGV(FORMAT,...) __android_log_print(ANDROID_LOG_DEBUG,"native_V",FORMAT,##__VA_ARGS__);

#define LOGI(FORMAT,...) __android_log_print(ANDROID_LOG_DEBUG,"native_I",FORMAT,##__VA_ARGS__);
#define LOGS(...) __android_log_print(ANDROID_LOG_DEBUG,"native_D","%s",##__VA_ARGS__);
#define LOGF(FORMAT,...) __android_log_print(ANDROID_LOG_DEBUG,"native_F",FORMAT,##__VA_ARGS__);

#endif //NATIVE_ANDROIDLOG_H
