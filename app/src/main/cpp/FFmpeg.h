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
#include "avcpp/filters/filtergraph.h"
#include "avcpp/filters/filtercontext.h"
#include "avcpp/dictionary.h"

#include <string>
#include <memory>
#include <utility>

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
     * @brief 统一的输出初始化入口：按功能名分派，参数全部取自 enc_par 命名空间
     *
     * 完成：打开输出 → 创建编码器/流 → 初始化 sws/swr → 写文件头，不执行编码。
     * 调用后按功能再调 encodeToFile()（截取用 encodeToFile(start,end)、
     * 亮度用 encodeToFile(brightness,gamma)、其余用 encodeToFile()）。
     *
     * 分派规则：
     * - "EncodeWithOtherEncoders"：用 enc_par::g_encoderVideoCodec /
     *   g_encoderAudioCodec 查找编码器，其余参数沿用源；复制源元数据
     * - "EncodeWithCompress"：用 enc_par::g_compressLevel 缩放码率/帧率/分辨率/采样率
     * - "EncodeWithTrim" / "EncodeWithBrightness"：复用源编码器与全部源参数；复制源元数据
     * - "EncodeWithMetadata"：复用源编码器与全部源参数；不复制源元数据（只写用户填写的字段）
     *
     * 输出路径取自 enc_par::g_outputPath，为空时返回 -EINVAL。
     *
     * @param functionName 功能名（与 Selecting 返回的 Function 一致）
     * @return 0 表示成功，负数表示错误码
     */
    int openOutput(const std::string& functionName);

    /**
     * @brief 编码参数：时间区间 + 亮度/伽马
     *
     * 亮度调整是否启用由 brightness/gamma 是否偏离默认值自动判定，
     * 不再需要单独的开关参数。
     */
    struct EncodeOptions {
        uint64_t startTimeUs = 0;           ///< 起始时间（微秒）
        uint64_t endTimeUs = (uint64_t)-1;  ///< 结束时间（微秒），(uint64_t)-1 表示不截止
        int8_t brightness = 0;              ///< 亮度偏移 -128~+127，0=不变
        float gamma = 1.0f;                 ///< 伽马值 0.5~3.0，1.0=不变
    };

    /**
     * @brief 编码循环（唯一实现），输出必须已由 openOutput() 初始化完成
     *
     * 读取输入包 → 解码 → 滤镜（brightness/gamma 非默认时插入 eq）→ 缩放/重采样
     * → 编码 → 写包 → 冲刷解码器/编码器 → writeTrailer。
     * 使用成员变量 outFmtCtx_/venc_/aenc_/outVStream_/outAStream_/swsCtx_/swrCtx_。
     *
     * @param opts 编码参数
     * @return 0 表示成功，负数表示错误码
     */
    int encodeToFile(const EncodeOptions& opts);

    /**
     * @brief 编码完整文件（等价于 encodeToFile(EncodeOptions{}))
     *
     * @return 0 表示成功，负数表示错误码
     */
    int encodeToFile();

    /**
     * @brief 设置输出文件的位置
     */
    inline void setOutputPath(std::string path) { outPath_ = path; }
    inline int videoStreamIndex() const { return videoStreamIndex_; }
    inline int audioStreamIndex() const { return audioStreamIndex_; }

    /**
     * @brief 额外写入一条元数据到输出文件
     *
     * 必须在 openOutput（写文件头）之前调用，否则元数据不会写入容器。
     * 若键已存在，flags=0 不会覆盖（保留原值）。
     *
     * @param entry 元数据键值对，key 和 value 均不能为空
     */
    void addMetadata(const AVDictionaryEntry *entry);

    /**
     * @brief 额外写入一条元数据到输出文件
     *
     * 必须在 openOutput（写文件头）之前调用，否则元数据不会写入容器。
     * 若键已存在，flags=0 不会覆盖（保留原值）。
     *
     * @param key 键值
     * @param value 键值所对应的值
     */
    void addMetadata(const char* key, const char* value);

private:

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

    // 封面流（纯音频文件）
    int coverStreamIndex_ = -1;

    // 用户手动添加的元数据（在输出上下文创建前暂存，writeHeader 前合并）
    AVDictionary *pendingMetadata_ = nullptr;

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
