#pragma once

#include "functions.h"
#include "strings.h"

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

    /**
     * @brief 使未被初始化的FFmpeg实例对象初始化
     */
    void init(androidOutStream &os, androidInStream &is);

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
    std::optional<uint64_t> getDuration() const {
        if (!fmtCtx_.duration().isValid()) return std::nullopt;
        else return fmtCtx_.duration().timestamp();
    }
    std::string getInputPath() const { return url_; }

    /**
     * @brief 获取媒体信息并输出到控制台
     */
    bool getMediaInfo() const;

    /**
     * @brief 用压缩参数完成输出初始化（计算 preset/码率，创建编码器/流，写文件头）
     *
     * 仅做输出侧初始化，不执行编码。调用后需再调用 encodeToFile() 写出压缩文件。
     * 复用源文件音视频编码器，不更换编码器；不使用 CRF，采用 ABR 码率模式。
     *
     * @param outputPath 输出文件路径（扩展名决定封装格式）
     * @param presetLevel 预设等级, 1体积最小, 10体积较大
     * @return 0 表示成功，负数表示错误码
     */
    int openOutputWithCompressMedia(const char* outputPath,
                                     int presetLevel);

    /**
     * @brief 纯编码循环（可复用），输出必须已由 openOutPutWithEncoder /
     *        openOutputWithCompressMedia 初始化完成
     *
     * 读取输入包 → 解码 → 缩放/重采样 → 编码 → 写包 → 冲刷解码器/编码器 → writeTrailer。
     * 使用成员变量 outFmtCtx_/venc_/aenc_/outVStream_/outAStream_/swsCtx_/swrCtx_。
     *
     * @return 0 表示成功，负数表示错误码
     */
    int encodeToFile();

    /**
     * @brief 带时间范围的编码循环（必须显式传入起止时间，无默认值）
     *
     * 与无参版本逻辑相同，但只编码 [startTimeUs, endTimeUs) 区间的内容。
     * 适用于截取/截断功能。参数均为微秒单位（AV_TIME_BASE）。
     *
     * @param startTimeUs 起始时间（微秒），从 0 开始
     * @param endTimeUs   结束时间（微秒），(uint64_t)-1 表示不截止
     * @return 0 表示成功，负数表示错误码
     */
    int encodeToFile(uint64_t startTimeUs, uint64_t endTimeUs);

    /**
     * @brief 带亮度/伽马调整的编码循环（编码完整文件）
     *
     * 对每一帧的 Y（亮度）平面做 brightness 线性偏移 + gamma 非线性校正，
     * U/V 色度平面不变。内部用 256 项查找表(LUT)加速，每帧只需查表无需浮点运算。
     *
     * @param brightness 亮度偏移，-100~+100（0=不变），正值变亮负值变暗
     * @param gamma      伽马值，建议 0.5~3.0（1.0=不变），<1 变亮 >1 变暗
     * @return 0 表示成功，负数表示错误码
     */
    int encodeToFile(int8_t brightness, float gamma = 1.0);

    /**
     * @brief 用指定编码器ID完成输出初始化（打开输出、创建编码器/流、写文件头）
     *
     * 负责全部输出侧初始化工作，完成后可直接调用 encodeToFile() 进行编码。
     *
     * @param videoID 视频编码器ID，AV_CODEC_ID_NONE 表示不输出视频
     * @param audioID 音频编码器ID，AV_CODEC_ID_NONE 表示不输出音频
     * @param videoBitrate 视频目标码率(bps)，0=使用编码器默认
     * @param presetStr 编码器preset字符串（如"medium"/"slow"），nullptr=不设置
     * @param targetFps 输出视频帧率，0=沿用源帧率
     * @param audioBitrate 音频目标码率(bps)，0=使用默认128kbps
     * @warning 调用此函数之前必须调用 setOutputPath() 设置 outPath_
     * @return 0 表示成功, 负数表示错误码
     */
    int openOutPutWithEncoder(AVCodecID videoID, AVCodecID audioID,
                               int64_t videoBitrate = 0,
                               const char* presetStr = "veryfast",
                               double targetFps = 0.0,
                               int64_t audioBitrate = 0,
                               int outWidth = 0,
                               int outHeight = 0,
                               int targetSampleRate = 0,
                               bool allowHardware = true);

    /**
     * @brief 直接复用源文件所有解码器参数初始化输出（分辨率/帧率/码率/采样率/声道布局均不变）
     *
     * 适用于"不需要在初始化编码器期间做任何参数变化"的场景（如快速转码、格式封装转换）。
     * 唯一会变化的是编码线程数，仍由 config.dat 配置的 encodeThreads 成员控制。
     * 视频/音频编码器 ID 直接取源流 codecpar->codec_id；若源编码器无对应 FFmpeg 编码器则失败。
     * 完成后可直接调用 encodeToFile() 进行编码。
     *
     * @param outputPath 输出文件路径（扩展名决定封装格式）
     * @return 0 表示成功，负数表示错误码
     */
    int openOutputWithSourceParams(const char* outputPath);

    /**
     * @brief 便捷重载：用指定名称的编码器初始化输出并编码写出文件
     *
     * 内部等价于：name→codec_id → openOutPutWithEncoder() → encodeToFile()。
     * 封装格式由输出路径的扩展名决定（如 .mp4/.mkv/.wav/.mp3/.flac）。
     *
     * @param outputPath 输出文件路径（扩展名决定封装格式）
     * @param videoEncoderName 视频编码器名称（如 "libx264"/"libx265"/"mpeg4"），
     *                         空指针或空串表示不输出视频流
     * @param audioEncoderName 音频编码器名称（如 "aac"/"libmp3lame"/"pcm_s16le"/"flac"），
     *                         空指针或空串表示不输出音频流
     * @return 0 表示成功，负数表示错误码
     *
     * @note 音频采样格式会根据编码器支持的格式自动匹配
     *       （aac→FLTP, pcm_s16le→S16, flac→S16 等）
     */
    int encodeToFile(const char* outputPath,
                     const char* videoEncoderName,
                     const char* audioEncoderName);

    /**
     * @brief 设置输出文件的位置
     */
    inline void setOutputPath(std::string path) { outPath_ = path; }
    inline int videoStreamIndex() const { return videoStreamIndex_; }
    inline int audioStreamIndex() const { return audioStreamIndex_; }

private:

    /**
     * @brief 带可选 LUT 的核心编码循环（所有 encodeToFile 重载的唯一实现）
     *
     * @param startTimeUs 起始时间（微秒）
     * @param endTimeUs   结束时间（微秒），(uint64_t)-1 表示不截止
     * @param brightnessLut         256 项亮度查找表，nullptr 表示不做亮度调整
     * @return 0 表示成功，负数表示错误码
     */
    int encodeToFileWithLut(uint64_t startTimeUs, uint64_t endTimeUs, const uint8_t* brightnessLut);

    /**
     * @brief 通用输出初始化（私有辅助函数，被 openOutPutWithEncoder /
     *        openOutputWithCompressMedia 各自独立调用，二者互不调用）
     *
     * 完成：打开输出 → 创建编码器 → 创建输出流 → 初始化 sws/swr → 写文件头。
     *
     * @param videoID 视频编码器ID，AV_CODEC_ID_NONE 表示不输出视频
     * @param audioID 音频编码器ID，AV_CODEC_ID_NONE 表示不输出音频
     * @param videoBitrate 视频目标码率(bps)，0=使用编码器默认
     * @param presetStr 编码器preset字符串，nullptr=不设置
     * @param targetFps 输出视频帧率，0=沿用源帧率
     * @param audioBitrate 音频目标码率(bps)，0=使用默认128kbps
     * @return 0 表示成功, 负数表示错误码
     */
    int initOutputContext(AVCodecID videoID, AVCodecID audioID,
                           int64_t videoBitrate,
                           const char* presetStr,
                           double targetFps,
                           int64_t audioBitrate,
                           int outWidth = 0,
                           int outHeight = 0,
                           int targetSampleRate = 0,
                           bool allowHardware = true,
                           bool keepSourceChannelLayout = false);

    bool inited = false;

    // 解码线程数(初始化输入式同步配置文件)
    uint8_t encodeThreads;

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

extern FFmpeg *ffmpeg;

extern "C" JNIEXPORT jboolean JNICALL
Java_com_kgmdecoder_app_Selecting_hasVideo(JNIEnv *env, jobject thiz);

extern "C" JNIEXPORT jboolean JNICALL
Java_com_kgmdecoder_app_Selecting_hasAudio(JNIEnv *env, jobject thiz);

extern "C" JNIEXPORT jlong JNICALL
Java_com_kgmdecoder_app_Selecting_durationUs(JNIEnv *env, jobject thiz);

extern "C" JNIEXPORT void JNICALL
Java_com_kgmdecoder_app_MainActivity_releaseFFmpeg(JNIEnv *env, jobject thiz);

// 释放全局 FFmpeg 实例
void releaseGlobalFFmpeg();
