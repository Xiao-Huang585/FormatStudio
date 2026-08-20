// avcpp 配置头文件 (avconfig.h)
// 用于 Android NDK 构建，手动生成以替代 CMake configure_file
// 通过包含 FFmpeg 版本头文件自动检测版本，适配 FFmpeg 5.x/6.x/7.x/8.x

#pragma once

// ====== 包含 FFmpeg 版本头文件（自动检测版本） ======
// 这些头文件只定义宏，不包含函数声明，可安全在 C++ 中使用
extern "C" {
#include <libavutil/version.h>
#include <libavcodec/version.h>
#include <libavformat/version.h>
#include <libavfilter/version.h>
}

// ====== 功能开关 ======
#define AVCPP_HAS_AVFORMAT 1
#define AVCPP_HAS_AVFILTER 1
#define AVCPP_HAS_AVDEVICE 0
#define AVCPP_CXX_STANDARD 23

// ====== 版本宏（从 FFmpeg 头文件自动派生） ======
// avcpp 的 avcompat.h 通过这些宏判断 FFmpeg 版本，
// 决定包含哪些头文件、使用哪些 API。
// 如果不定义，默认为 0，avcpp 会误判为古老版本，
// 从而包含已删除的头文件（如 avfiltergraph.h）。

// --- avutil ---
#define AVCPP_AVUTIL_VERSION_MAJOR  LIBAVUTIL_VERSION_MAJOR
#define AVCPP_AVUTIL_VERSION_MINOR  LIBAVUTIL_VERSION_MINOR
#define AVCPP_AVUTIL_VERSION_INT    AV_VERSION_INT(LIBAVUTIL_VERSION_MAJOR, LIBAVUTIL_VERSION_MINOR, LIBAVUTIL_VERSION_MICRO)

// --- avcodec ---
#define AVCPP_AVCODEC_VERSION_MAJOR LIBAVCODEC_VERSION_MAJOR
#define AVCPP_AVCODEC_VERSION_MINOR LIBAVCODEC_VERSION_MINOR
#define AVCPP_AVCODEC_VERSION_INT   AV_VERSION_INT(LIBAVCODEC_VERSION_MAJOR, LIBAVCODEC_VERSION_MINOR, LIBAVCODEC_VERSION_MICRO)

// --- avformat ---
#define AVCPP_AVFORMAT_VERSION_MAJOR LIBAVFORMAT_VERSION_MAJOR
#define AVCPP_AVFORMAT_VERSION_MINOR LIBAVFORMAT_VERSION_MINOR
#define AVCPP_AVFORMAT_VERSION_INT   AV_VERSION_INT(LIBAVFORMAT_VERSION_MAJOR, LIBAVFORMAT_VERSION_MINOR, LIBAVFORMAT_VERSION_MICRO)

// --- avfilter ---
#define AVCPP_AVFILTER_VERSION_MAJOR LIBAVFILTER_VERSION_MAJOR
#define AVCPP_AVFILTER_VERSION_INT   AV_VERSION_INT(LIBAVFILTER_VERSION_MAJOR, LIBAVFILTER_VERSION_MINOR, LIBAVFILTER_VERSION_MICRO)

// ====== 注意 ======
// 以下派生宏由 avcompat.h 根据上面的版本宏自动计算，无需在此手动定义：
//   AVCPP_USE_CODECPAR              — FFmpeg 5.0+ (avcodec >= 59)
//   AVCPP_API_NEW_CHANNEL_LAYOUT    — FFmpeg 5.1+ (avutil > 57.24)
//   AVCPP_API_FRAME_NUM             — FFmpeg 6.0+ (avcodec > 60.2)
//   AVCPP_API_AVFORMAT_URL          — FFmpeg 4.0+ (avformat > 58.7)
//   AVCPP_API_FRAME_KEY             — FFmpeg 6.1+ (avutil > 58.29)
//   AVCPP_API_AVCODEC_CLOSE         — FFmpeg < 7.0 (avcodec < 61)
//   AVCPP_API_AVCODEC_NEW_INIT_PACKET — FFmpeg 4.0+ (avcodec >= 58)
//   AVCPP_API_AVBUFFER_SIZE_T       — FFmpeg 5.0+ (avutil >= 57)
