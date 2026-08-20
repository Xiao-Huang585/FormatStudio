#pragma once

#include "functions.h"

// ============================
// avcpp 头文件（替代原生 FFmpeg C API）
// ============================
#include "avcpp/av.h"
#include "avcpp/ffmpeg.h"
#include "avcpp/format.h"
#include "avcpp/formatcontext.h"
#include "avcpp/codec.h"
#include "avcpp/codeccontext.h"
#include "avcpp/stream.h"
#include "avcpp/packet.h"
#include "avcpp/frame.h"
#include "avcpp/videorescaler.h"
#include "avcpp/audioresampler.h"
#include "avcpp/pixelformat.h"
#include "avcpp/sampleformat.h"
#include "avcpp/rational.h"
#include "avcpp/timestamp.h"
#include "avcpp/dictionary.h"
#include "avcpp/channellayout.h"

#include <string>
#include <memory>

/**
 * @brief 基于 avcpp 的媒体处理类
 *
 * 使用 avcpp (C++ wrapper for FFmpeg) 替代原生 FFmpeg C API
 * 所有 FFmpeg 资源通过 RAII 自动管理，无需手动释放
 *
 * 功能：
 * - 打开输入媒体文件/流
 * - 获取媒体信息（格式、时长、码率、视频/音频参数）
 * - 压缩/转码媒体文件（支持纯视频、纯音频、音视频混合）
 */
class FFmpeg {
public:
    FFmpeg(androidOutStream &os, androidInStream &is);
    ~FFmpeg();

    // 禁止拷贝
    FFmpeg(const FFmpeg&) = delete;
    FFmpeg& operator=(const FFmpeg&) = delete;

    /**
     * @brief 打开输入媒体文件/流，并初始化视频/音频解码器上下文
     * @param url 媒体路径
     * @return 0 表示成功，负数表示错误码
     */
    int openInput(const char* url);

    /**
     * @brief 关闭输入并释放所有资源
     */
    void close();

    bool hasVideo() const { return videoStreamIndex_ >= 0; }
    bool hasAudio() const { return audioStreamIndex_ >= 0; }

    /**
     * @brief 获取媒体信息并输出到控制台
     */
    bool getMediaInfo() const;

    /**
     * @brief 压缩/转码当前已打开的媒体文件
     *
     * @param outputPath 输出文件路径
     * @param targetBitRateKbps 目标视频码率(kbps)，默认 800
     * @param targetWidth 目标宽度，0 表示保持原始
     * @param targetHeight 目标高度，0 表示保持原始
     * @param crf x264 CRF 质量值（18-28），默认 23
     * @param preset x264 编码速度预设
     * @return 0 表示成功，负数表示错误码
     */
    int compressMedia(const char* outputPath,
                      int targetBitRateKbps = 800,
                      int targetWidth = 0,
                      int targetHeight = 0,
                      int crf = 23,
                      const char* preset = "medium");

    /**
     * @brief 用指定编码器打开输出
     * @param id 打开输出文件使用的编码器id
     * @warning 调用此函数之前必须调用 setOutputPath()
     * @return 0 表示成功, 负数表示错误码
     */
    int openOutPutWithEncoder(AVCodecID videoId, AVCodecID audioID);

    /**
     * @brief 设置输出文件的位置
     */
    inline void setOutputPath(std::string path) { outPath_ = path; }
    inline int videoStreamIndex() const { return videoStreamIndex_; }
    inline int audioStreamIndex() const { return audioStreamIndex_; }

private:
    // avcpp 格式上下文（RAII 管理）
    av::FormatContext fmtCtx_;
    av::FormatContext outFmtCtx_;

    // 视频解码器（RAII 管理）
    av::VideoDecoderContext vdec_;
    int videoStreamIndex_ = -1;

    // 音频解码器（RAII 管理）
    av::AudioDecoderContext adec_;
    int audioStreamIndex_ = -1;

    // 视频编码器
    av::VideoEncoderContext venc_;
    av::Stream outVStream_;
    SwsContext *swsCtx_ = nullptr;

    // 音频编码器
    av::AudioEncoderContext aenc_;
    av::Stream outAStream_;
    SwrContext *swrCtx_ = nullptr;

    // 文件路径
    std::string url_;
    std::string outPath_;

    // I/O 引用
    androidOutStream &cout;
    androidInStream &cin;
};
