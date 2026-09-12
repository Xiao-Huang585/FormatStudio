#include "FFmpeg.h"

// 原生 FFmpeg C API（仅用于低级操作：sws/swr/metadata）
extern "C" {
#include <libavutil/pixdesc.h>
#include <libavutil/channel_layout.h>
#include <libavutil/audio_fifo.h>
#include <libswscale/swscale.h>
#include <libswresample/swresample.h>
#include <libavutil/opt.h>
#include <libavutil/imgutils.h>
#include <libavformat/avio.h>
#include <libavutil/cpu.h>
#include <libavformat/avformat.h>
#include <libavutil/log.h>
}

FFmpeg *ffmpeg = nullptr;

// FFmpeg 内部日志转发到 logcat（用于调试编码器 EINVAL 根因）
static void ffmpegLogToLogcat(void *ptr, int level, const char *fmt, va_list vl)
{
    if (level > AV_LOG_WARNING) return; // 只转发 WARNING 及更严重
    char buf[1024] = {0};
    vsnprintf(buf, sizeof(buf), fmt, vl);
    LOGD("ffmpeg[%d]: %s", level, buf);
}

// ============================
// 构造/析构
// ============================
FFmpeg::FFmpeg(androidOutStream &os, androidInStream &is)
        : cout(os), cin(is), inited(true), outPath_("/sdcard/Download/default.mp4") {
    av_log_set_level(AV_LOG_DEBUG);
    av_log_set_callback(ffmpegLogToLogcat);
}

FFmpeg::~FFmpeg() {
    close();
}

// ============================
// 关闭并释放资源（avcpp RAII 自动处理大部分）
// ============================
void FFmpeg::close() {
    if (swsCtx_) {
        sws_freeContext(swsCtx_);
        swsCtx_ = nullptr;
    }
    if (swrCtx_) {
        swr_free(&swrCtx_);
        swrCtx_ = nullptr;
    }
    vdec_ = av::VideoDecoderContext();
    adec_ = av::AudioDecoderContext();

    // 重置输出侧编码器与流（确保下次初始化从干净状态开始）
    venc_ = av::VideoEncoderContext();
    aenc_ = av::AudioEncoderContext();
    outVStream_ = av::Stream();
    outAStream_ = av::Stream();

    videoStreamIndex_ = -1;
    audioStreamIndex_ = -1;

    if (fmtCtx_.isOpened()) {
        fmtCtx_.close();
    }

    if (outFmtCtx_.isOpened()) {
        outFmtCtx_.close();
    }

    outPath_ = "/sdcard/Download/default.mp4";
    url_.clear();
}

// ============================
// 打开输入媒体文件
// ============================
int FFmpeg::openInput(const char* url) {
    if (!url || !url[0]) {
        return -EINVAL;
    }

    close();

    std::error_code ec;

    fmtCtx_.openInput(url, ec);
    if (ec) {
        cout << String("打开文件失败: ", "Fail to open file: ") << ec.message() << endl;
        return ec.value();
    }

    fmtCtx_.findStreamInfo(ec);
    if (ec) {
        cout << String("获取流信息失败: ", "Fail to receive stream info: ") << ec.message() << endl;
        close();
        return ec.value();
    }

    url_ = url;

    for (size_t i = 0; i < fmtCtx_.streamsCount(); i++) {
        av::Stream st = fmtCtx_.stream(i);
        AVMediaType type = st.mediaType();

        if (type == AVMEDIA_TYPE_VIDEO && videoStreamIndex_ < 0) {
            AVCodecParameters* codecpar = st.raw()->codecpar;
            if (codecpar->width == 0 || codecpar->height == 0) {
                continue;
            }

            vdec_ = av::VideoDecoderContext(st);
            vdec_.raw()->thread_count = av_cpu_count() - 4 <= 0 ? 2 : av_cpu_count() - 4;
            if (vdec_.raw()->thread_count > 6) vdec_.raw()->thread_count = 6;
            vdec_.raw()->thread_type  = FF_THREAD_FRAME | FF_THREAD_SLICE;
            vdec_.open(ec);
            if (ec) {
                LOGD("视频解码器打开失败: %s", ec.message().c_str());
                vdec_ = av::VideoDecoderContext();
                continue;
            }
            videoStreamIndex_ = static_cast<int>(i);
        }
        else if (type == AVMEDIA_TYPE_AUDIO && audioStreamIndex_ < 0) {
            adec_ = av::AudioDecoderContext(st);
            adec_.open(ec);
            if (ec) {
                LOGD("音频解码器打开失败: %s", ec.message().c_str());
                adec_ = av::AudioDecoderContext();
                continue;
            }
            audioStreamIndex_ = static_cast<int>(i);
        }
    }

    return 0;
}

// ============================
// 辅助：根据编码器支持的采样格式自动挑选输出采样格式
// ============================
static AVSampleFormat pickEncoderSampleFormat(const AVCodec *codec) {
    if (!codec) {
        return AV_SAMPLE_FMT_FLTP;
    }

    const enum AVSampleFormat *sampleFmts = nullptr;
#if LIBAVCODEC_VERSION_INT >= AV_VERSION_INT(61, 13, 100)
    // FFmpeg 7.1+ 的官方接口（sample_fmts 字段已废弃）
    avcodec_get_supported_config(nullptr, codec, AV_CODEC_CONFIG_SAMPLE_FORMAT, 0,
                                 reinterpret_cast<const void**>(&sampleFmts), nullptr);
#else
    sampleFmts = codec->sample_fmts;
#endif

    if (!sampleFmts) {
        return AV_SAMPLE_FMT_FLTP;
    }
    // 优先 FLTP（aac 等常用），否则取第一个支持的格式
    for (int i = 0; sampleFmts[i] != AV_SAMPLE_FMT_NONE; i++) {
        if (sampleFmts[i] == AV_SAMPLE_FMT_FLTP) {
            return AV_SAMPLE_FMT_FLTP;
        }
    }
    return sampleFmts[0];
}

// ============================
// 用指定编码器ID完成输出初始化（公开入口，独立调用 initOutputContext）
// ============================
int FFmpeg::openOutPutWithEncoder(AVCodecID videoID, AVCodecID audioID,
                                   int64_t videoBitrate,
                                   const char* presetStr,
                                   double targetFps,
                                   int64_t audioBitrate) {
    return initOutputContext(videoID, audioID, videoBitrate, presetStr, targetFps, audioBitrate);
}

// ============================
// 通用输出初始化（私有辅助函数）
// 被 openOutPutWithEncoder / openOutputWithCompressMedia 各自独立调用，二者互不调用
// 完成：打开输出 → 创建编码器 → 创建输出流 → 初始化 sws/swr → 写文件头
// ============================
int FFmpeg::initOutputContext(AVCodecID videoID, AVCodecID audioID,
                               int64_t videoBitrate,
                               const char* presetStr,
                               double targetFps,
                               int64_t audioBitrate) {
    if (!fmtCtx_.isOpened()) {
        cout << String("未打开输入文件...", "Didn't open input file...") << endl;
        LOGD("未打开输出文件...");
        return -EINVAL;
    }
    if (!hasVideo() && !hasAudio()) {
        cout << String("未找到音视频流...", "Can not find video/audio stream...") << endl;
        LOGD("未找到音视频流...");
        return -EINVAL;
    }

    bool wantVideo = hasVideo() && videoID != AV_CODEC_ID_NONE;
    bool wantAudio = hasAudio() && audioID != AV_CODEC_ID_NONE;

    // 根据输出路径扩展名猜测封装格式，并检查容器兼容性
    const AVOutputFormat *ofmt = av_guess_format(nullptr, outPath_.c_str(), nullptr);
    if (!ofmt) {
        cout << String("无法根据扩展名识别输出格式: ", "Can't recognize output format by extension: ")
             << outPath_ << endl;
        LOGD("无法根据扩展名识别输出格式: %s", outPath_.c_str());
        return -EINVAL;
    }
    if (wantVideo && avformat_query_codec(ofmt, videoID, FF_COMPLIANCE_NORMAL) == 0) {
        cout << String("容器不支持视频编码器: ", "Container doesn't support video encoder: ")
             << avcodec_get_name(videoID);
        cout << String("，已跳过视频流", ", video stream skipped") << endl;
        LOGD("容器不支持视频编码器: %s, 已跳过视频流", avcodec_get_name(videoID));
        wantVideo = false;
    }
    if (wantAudio && avformat_query_codec(ofmt, audioID, FF_COMPLIANCE_NORMAL) == 0) {
        cout << String("容器不支持音频编码器 ", "Container doesn't support audio encoder ")
             << avcodec_get_name(audioID);
        cout << String("，已跳过音频流", ", audio stream skipped") << endl;
        LOGD("容器不支持音频编码器: %s, 已跳过音频流", avcodec_get_name(audioID));
        wantAudio = false;
    }
    if (!wantVideo && !wantAudio) {
        cout << String("没有可编码的音视频流...", "No audio/video stream to encode...") << endl;
        LOGD("没有可编码的音视频流...");
        return -EINVAL;
    }

    std::error_code ec;

    // 打开输出（只调用一次）
    outFmtCtx_.openOutput(outPath_, ec);
    if (ec) {
        cout << String("打开输出文件失败...", "Fail to open output file...") << endl;
        LOGD("打开输出文件失败...");
        return ec.value();
    }

    const bool needGlobalHeader = !!(outFmtCtx_.raw()->oformat->flags & AVFMT_GLOBALHEADER);

    // ========== 视频初始化 ==========
    if (wantVideo) {
        // 尝试启用Android硬件编码
        const AVCodec* hwCodec = nullptr;
        if (videoID == AV_CODEC_ID_H264) hwCodec = avcodec_find_encoder_by_name("h264_mediacodec");
        else if (videoID == AV_CODEC_ID_H265) hwCodec = avcodec_find_encoder_by_name("hevc_mediacodec");

        av::Codec vCodec = hwCodec ? av::Codec(hwCodec) : av::findEncodingCodec(videoID);
        const bool isHardWare = (hwCodec != nullptr);
        if (vCodec.isNull()) {
            cout << String("找不到视频编码器: ", "Can not find video encoder: ") << videoID << endl;
            LOGD("找不到视频编码器: %s", avcodec_get_name(videoID));
            return -EINVAL;
        }


        venc_ = av::VideoEncoderContext(vCodec);

        int outWidth = vdec_.width() & ~1;
        int outHeight = vdec_.height() & ~1;
        venc_.raw()->width = outWidth;
        venc_.raw()->height = outHeight;
        venc_.raw()->pix_fmt = vdec_.pixelFormat();

        AVRational inFrameRate = fmtCtx_.raw()->streams[videoStreamIndex_]->avg_frame_rate;
        if (inFrameRate.den <= 0 || inFrameRate.num <= 0) {
            inFrameRate = {25, 1};
        }
        // 外部指定目标帧率则覆盖
        if (targetFps > 1.0) {
            AVRational target = av_d2q(targetFps, 1000);
            inFrameRate = target;
        }
        // 启用多线程编码(软解)
        if (!isHardWare) {
            LOGD("使用软件编码");
            int threads = av_cpu_count() <= 0 ? 2 : av_cpu_count() - 2;
            if (videoID == AV_CODEC_ID_MPEG4) {
                // mpeg4 不支持 frame 级多线程，但支持 slice 级
                venc_.raw()->thread_count = threads;
                venc_.raw()->thread_type  = FF_THREAD_SLICE;
            } else {
                venc_.raw()->thread_count = threads;
                venc_.raw()->thread_type  = FF_THREAD_FRAME;
            }
        } else {
            cout << String("启用H264/H265硬编码", "Enabled H264/H265 hardware encoding");
            LOGD("启用H264/H265硬编码");
            venc_.raw()->thread_count = av_cpu_count() <= 0 ? 2 : av_cpu_count() - 2;
            if (venc_.raw()->thread_count > 6) venc_.raw()->thread_count = 6;
            if (venc_.raw()->priv_data) {
                av_opt_set(venc_.raw()->priv_data, "bitrate-mode", "VBR", 0);
            }
        }

        // 帧率四舍五入到整数，mpeg4 对非整数帧率支持不好
        int fpsInt = (int)(av_q2d(inFrameRate) + 0.5);
        if (fpsInt < 1) fpsInt = 25;
        AVRational encFrameRate = {fpsInt, 1};
        venc_.raw()->framerate = encFrameRate;
        venc_.raw()->time_base = av_inv_q(encFrameRate);
        venc_.raw()->gop_size = 50;

        // 码率与 preset 设置
        if (videoBitrate > 0) {
            venc_.raw()->bit_rate = videoBitrate;
            // 外部传入 bitrate 时优先使用 ABR，不使用 CRF
            if (presetStr && presetStr[0]) {
                venc_.setOption("preset", presetStr);
            }
        } else if (videoID == AV_CODEC_ID_H264 || videoID == AV_CODEC_ID_H265) {
            // 未传 bitrate 时回退到默认 preset+crf
            venc_.setOption("preset", presetStr && presetStr[0] ? presetStr : "medium");
            if (videoID == AV_CODEC_ID_H264 || videoID == AV_CODEC_ID_H265) venc_.setOption("crf", "27");
        } else {
            venc_.raw()->bit_rate = 2000000;
        }

        // mpeg4 等编码器需要 GLOBAL_HEADER
        venc_.raw()->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;

//      venc_.open(ec);
        int openRet = avcodec_open2(venc_.raw(), vCodec.raw(), nullptr);
        LOGD("venc after open: %dx%d pix=%d tb=%d/%d fr=%d/%d br=%ld gop=%d flags=0x%x openRet=%d",
             venc_.width(), venc_.height(), static_cast<int>(venc_.pixelFormat().get()),
             venc_.raw()->time_base.num, venc_.raw()->time_base.den,
             venc_.raw()->framerate.num, venc_.raw()->framerate.den,
             venc_.bitRate(), venc_.gopSize(), venc_.raw()->flags, openRet);

        if (openRet < 0) {
            char errbuf[256] = {0};
            av_strerror(openRet, errbuf, sizeof(errbuf));
            cout << String("打开视频编码器失败: ", "Fail to open video encoder: ") << errbuf << endl;
            LOGD("打开视频编码器%s失败: ret=%d, %s", avcodec_get_name(videoID), openRet, errbuf);
            return openRet;
        }

        outVStream_ = outFmtCtx_.addStream(venc_, ec);
        if (ec) {
            cout << String("创建输出视频流失败: ", "Fail to create output video stream: ")
                 << ec.message() << endl;
            LOGD("创建输出视频流失败: %s", ec.message().c_str());
            return ec.value();
        }

        swsCtx_ = sws_getContext(
                vdec_.width(), vdec_.height(), vdec_.pixelFormat(),
                outWidth, outHeight, AV_PIX_FMT_YUV420P,
                SWS_BILINEAR, nullptr, nullptr, nullptr
        );
        if (!swsCtx_) {
            cout << String("初始化像素格式转换失败...", "Fail to init sws...") << endl;
            LOGD("初始化像素格式失败...");
            return -ENOMEM;
        }

        cout << String("视频编码器: ", "Video encoder: ") << avcodec_get_name(videoID)
             << " (" << outWidth << "x" << outHeight << ")" << endl;
    }

    // ========== 音频初始化 ==========
    if (wantAudio) {
        av::Codec aCodec = av::findEncodingCodec(audioID);
        if (aCodec.isNull()) {
            cout << String("找不到音频编码器, 已取消音频...",
                           "Can not find audio encoder, audio stream canceled.") << endl;
            LOGD("找不到音频编码器%s, 已取消音频...", avcodec_get_name(aCodec.id()));
        } else {
            aenc_ = av::AudioEncoderContext(aCodec);

            AVSampleFormat outSampleFmt = pickEncoderSampleFormat(aCodec.raw());
            aenc_.setSampleRate(adec_.sampleRate());
            aenc_.setSampleFormat(outSampleFmt);
            aenc_.setBitRate(audioBitrate > 0 ? audioBitrate : 128 * 1000);
            aenc_.setTimeBase(av::Rational(1, adec_.sampleRate()));

            AVChannelLayout stereoLayout = AV_CHANNEL_LAYOUT_STEREO;
            av_channel_layout_copy(&aenc_.raw()->ch_layout, &stereoLayout);

            if (needGlobalHeader) {
                aenc_.raw()->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
            }

            aenc_.open(ec);
            if (ec) {
                cout << String("打开音频编码器失败: ", "Fail to open audio encoder: ")
                     << ec.message() << endl;
                LOGD("打开音频编码器%s失败: %s", avcodec_get_name(aCodec.id()), ec.message().c_str());
                aenc_ = av::AudioEncoderContext();
            } else {
                outAStream_ = outFmtCtx_.addStream(aenc_, ec);
                // 复制 bit_rate 到 codecpar
                if (outAStream_.isValid()) {
                    outAStream_.raw()->codecpar->bit_rate = aenc_.raw()->bit_rate;
                }
                if (ec) {
                    cout << String("创建输出音频流失败: ",
                                   "Fail to create output audio stream: ") << ec.message() << endl;
                    LOGD("创建输出音频流失败: %s", ec.message().c_str());
                    aenc_ = av::AudioEncoderContext();
                } else {
                    AVChannelLayout outLayout = AV_CHANNEL_LAYOUT_STEREO;
                    AVChannelLayout inLayout = {};
                    av_channel_layout_copy(&inLayout, &adec_.raw()->ch_layout);

                    int swrRet = swr_alloc_set_opts2(&swrCtx_,
                                                     &outLayout, outSampleFmt, adec_.sampleRate(),
                                                     &inLayout, adec_.sampleFormat(),
                                                     adec_.sampleRate(), 0, nullptr
                    );
                    av_channel_layout_uninit(&inLayout);
                    if (swrCtx_ && swrRet >= 0) {
                        swrRet = swr_init(swrCtx_);
                        if (swrRet < 0) {
                            cout << String("初始化重采样失败...", "Fail to init swr...") << endl;
                            LOGD("初始化重采样失败...");
                            swr_free(&swrCtx_);
                            swrCtx_ = nullptr;
                        }
                    }
                    cout << String("音频编码器: ", "Audio encoder: ") << avcodec_get_name(audioID) << endl;
                    LOGI("音频编码器: %s", avcodec_get_name(audioID));
                }
            }
        }
    }

    // 自动避免负时间戳：MP3 编码器有编码延迟(约 1105 样本)，导致第一个音频包 dts 为负，
    // AVI muxer 对负/重复 dts 会报 "non monotonically increasing dts" 而写包失败。
    // av_interleaved_write_frame 会按此选项把所有流包时间戳整体偏移，使最小 dts >= 0。
    outFmtCtx_.raw()->avoid_negative_ts = AVFMT_AVOID_NEG_TS_MAKE_ZERO;

    // 写文件头
    outFmtCtx_.writeHeader(ec);
    if (ec) {
        cout << String("写文件头失败: ", "Fail to write header: ") << ec.message() << endl;
        cout << String("（提示: 输出扩展名对应的容器可能不支持所选编码器组合）",
                       "(Hint: the container of the output extension may not support the selected encoders)")
             << endl;
        return ec.value();
        LOGD("写文件头失败: %s \n(提示: 输出扩展名对应的容器可能不支持所选编码器组合)", ec.message().c_str());
    }

    return 0;
}

// ============================
// 获取媒体信息
// ============================
bool FFmpeg::getMediaInfo() const {
    if (!fmtCtx_.isOpened()) {
        cout << String("未打开媒体文件", "Didn't open file.") << endl;
        return false;
    }

    const AVFormatContext* rawCtx = fmtCtx_.raw();

    cout << String("===== 媒体信息 =====", "=== Media information ===") << endl;
    cout << String("文件路径: ", "Path: ") << url_ << endl;
    cout << String("封装格式: ", "Format: ") <<
         (rawCtx->iformat ? String(rawCtx->iformat->name, rawCtx->iformat->name) : String("未知", "Unknown")) << endl;

    const int64_t fileSize = avio_size(rawCtx->pb);
    if ((fileSize / (1024.0 * 1024.0 * 1024.0 * 1024.0)) > 1.5)
        cout << String("文件大小(Tb): ", "File size(Tb): ") << (fileSize / (1024.0 * 1024.0 * 1024.0 * 1024.0)) << endl;
    else if (fileSize / (1024.0 * 1024.0 * 1024.0) > 1.5)
        cout << String("文件大小(Gb): ", "File size(Gb): ") << (fileSize / (1024.0 * 1024.0 * 1024.0)) << endl;
    else if (fileSize / (1024.0 * 1024.0) > 1.5)
        cout << String("文件大小(Mb): ", "File size(Mb): ") << (fileSize / (1024.0 * 1024.0)) << endl;
    else if (fileSize / (1024.0) > 1.5)
        cout << String("文件大小(Kb): ", "File size(Kb):") << (fileSize / (1024.0)) << endl;
    else
        cout << String("文件大小(byte): ", "File size(byte): ") << fileSize << endl;

    if (rawCtx->duration != AV_NOPTS_VALUE) {
        const std::array<int, 4> duration =
                secondsToMicroseconds(rawCtx->duration / (AV_TIME_BASE / 1000));
        cout << String("时长: ", "Duration: ") << duration[0] << ":" << duration[1] << ":"
             << duration[2] << "." << duration[3] << endl;
    } else {
        cout << String("时长: 未知", "Duration: Unknown") << endl;
    }

    if (rawCtx->bit_rate > 0) {
        cout << String("总码率: ", "Total bitrate: ") << (rawCtx->bit_rate / 1000) << " kbps" << endl;
    } else {
        cout << String("总码率: 未知", "Total bitrate: Unknown") << endl;
    }

    cout << String("流的数量: ", "Stream number: ") << fmtCtx_.streamsCount() << endl;

    if (hasVideo()) {
        AVStream* vStream = rawCtx->streams[videoStreamIndex_];
        AVRational frameRate = vStream->avg_frame_rate;

        cout << String("\n[视频流]", "\n[Video Stream]") << endl;
        cout << String("  流索引: ", "  Index: ") << videoStreamIndex_ << endl;
        cout << String("  分辨率: ", "  Resolution: ") << vdec_.width() << "x" << vdec_.height() << endl;

        const char* pixName = av_get_pix_fmt_name(vdec_.pixelFormat());
        cout << String("  像素格式: ", "  Pixel format: ") << (pixName ? String(pixName, pixName) : String("未知", "Unknown")) << endl;

        if (frameRate.den > 0 && frameRate.num > 0) {
            cout << String("  帧率: ", "  Frame rate: ") << av_q2d(frameRate) << " fps" << endl;
        }

        AVCodecParameters* codecpar = vStream->codecpar;
        cout << String("  编码器: ", "  Encoder: ") << avcodec_get_name(codecpar->codec_id) << endl;
        if (codecpar->bit_rate > 0) {
            cout << String("  码率: ", "  Bitrate:") << (codecpar->bit_rate / 1000) << " kbps" << endl;
        }
    }

    if (hasAudio()) {
        AVStream* aStream = rawCtx->streams[audioStreamIndex_];

        cout << String("\n[音频流]", "\n[Audio Stream]") << endl;
        cout << String("  流索引: ", "  Index: ") << audioStreamIndex_ << endl;
        cout << String("  采样率: ", "  Sample rate: ") << adec_.sampleRate() << " Hz" << endl;

        char chLayoutDesc[256] = {0};
        av_channel_layout_describe(&adec_.raw()->ch_layout, chLayoutDesc, sizeof(chLayoutDesc));
        cout << String("  声道布局: ", "  Channel layout: ") << chLayoutDesc << endl;

        AVCodecParameters* codecpar = aStream->codecpar;
        cout << String("  编码器: ", "  Encoder: ") << avcodec_get_name(codecpar->codec_id) << endl;

        if (codecpar->bit_rate > 0) {
            cout << String("  码率: ", "  Bitrate:") << (codecpar->bit_rate / 1000) << " kbps" << endl;
        }

        AVDictionaryEntry* entry = nullptr;
        entry = av_dict_get(rawCtx->metadata, "artist", nullptr, 0);
        if (entry && entry->value)
            cout << String("  艺术家: ", "Artist: ") << entry->value << endl;
        entry = av_dict_get(rawCtx->metadata, "album", nullptr, 0);
        if (entry && entry->value)
            cout << String("  专辑: ", "  Album") << entry->value << endl;
        entry = av_dict_get(rawCtx->metadata, "encoder", nullptr, 0);
        if (entry && entry->value)
            cout << String("  编码器: ", "  Encoder: ") << entry->value << endl;
    }

    cout.flush();
    return true;
}

// ============================
// 用压缩参数完成输出初始化（仅初始化，不编码；调用后需再调 encodeToFile() 写出）
// preset 1=体积最小, 10=体积较大；不改变源编码器；不使用CRF
// ============================
int FFmpeg::openOutputWithCompressMedia(const char* outputPath,
                                          int presetLevel) {
    if (presetLevel < 1 || presetLevel > 10)
        throw std::invalid_argument("presetLevel must be 1~10!");

    if (!fmtCtx_.isOpened()) {
        cout << String("未打开输入文件...", "Didn't open input file...") << endl;
        return -EINVAL;
    }

    const std::vector<std::string> presetStrTable = {
            "placebo", "veryslow", "slower", "slow", "medium",
            "fast", "veryfast", "superfast", "ultrafast"
    };
    const std::string preset = presetStrTable.at(presetLevel - 1);

    // 码率缩放系数: preset=1 → 0.3(最小体积), preset=10 → 1.8(较大体积)
    const double scaleFactor = 0.3 + (presetLevel - 1) * (1.8 - 0.3) / 9.0;

    // 读取输入流原始参数（复用源编码器，不更换）
    AVCodecID videoID = AV_CODEC_ID_NONE;
    AVCodecID audioID = AV_CODEC_ID_NONE;
    int64_t srcVideoBitrate = 0;
    int64_t srcAudioBitrate = 0;
    double srcVideoFps = 25.0;

    if (hasVideo()) {
        AVStream* st = fmtCtx_.raw()->streams[videoStreamIndex_];
        videoID = st->codecpar->codec_id;
        srcVideoBitrate = st->codecpar->bit_rate;
        if (srcVideoBitrate <= 0) srcVideoBitrate = 2000000;
        AVRational fr = st->avg_frame_rate;
        if (fr.num > 0 && fr.den > 0) {
            srcVideoFps = av_q2d(fr);
        }
    }
    if (hasAudio()) {
        AVStream* st = fmtCtx_.raw()->streams[audioStreamIndex_];
        audioID = st->codecpar->codec_id;
        srcAudioBitrate = st->codecpar->bit_rate;
        if (srcAudioBitrate <= 0) srcAudioBitrate = 128000;
    }

    // 计算输出码率（ABR 模式，不使用 CRF）
    int64_t outVideoBitrate = static_cast<int64_t>(std::llround(srcVideoBitrate * scaleFactor));
    int64_t outAudioBitrate = static_cast<int64_t>(std::llround(srcAudioBitrate * scaleFactor));

    // 判断编码器是否支持 preset 私有选项（x264/x265/vp9 支持）
    bool codecSupportPreset = false;
    if (hasVideo()) {
        if (videoID == AV_CODEC_ID_H264 || videoID == AV_CODEC_ID_H265 ||
            videoID == AV_CODEC_ID_VP9) {
            codecSupportPreset = true;
        }
    }

    // 不支持 preset 的编码器回退：适度降低帧率（最低 10fps，不高于源帧率）
    double targetFps = 0.0;
    if (hasVideo() && !codecSupportPreset) {
        targetFps = srcVideoFps * (0.4 + scaleFactor * 0.4);
        if (targetFps < 10.0) targetFps = 10.0;
        if (targetFps > srcVideoFps) targetFps = srcVideoFps;
    }

    // 设置输出路径
    outPath_ = outputPath;

    cout << "===== 开始压缩 =====" << endl;
    cout << String("输出: ", "Output: ") << outputPath << endl;
    cout << String("预设等级: ", "Preset: ") << presetLevel << " (" << preset << ")" << endl;
    cout << String("码率缩放系数: ", "Scale factor: ") << scaleFactor << endl;
    if (hasVideo()) {
        cout << String("视频码率: ", "Video bitrate: ") << (outVideoBitrate / 1000) << " kbps" << endl;
        if (!codecSupportPreset) {
            cout << String("目标帧率: ", "Target fps: ") << targetFps << endl;
        }
    }
    if (hasAudio()) {
        cout << String("音频码率: ", "Audio bitrate: ") << (outAudioBitrate / 1000) << " kbps" << endl;
    }
    cout.flush();

    // 输出初始化（直接调用私有 initOutputContext，与 openOutPutWithEncoder 独立，互不调用）
    int ret = initOutputContext(
            videoID, audioID,
            outVideoBitrate,
            codecSupportPreset ? preset.c_str() : nullptr,
            targetFps,
            outAudioBitrate
    );
    if (ret != 0) {
        return ret;
    }

    // 仅完成输出初始化，编码由调用方后续调用 encodeToFile() 执行
    return 0;
}

// ============================
// 纯编码循环（可复用）：输出必须已由 openOutPutWithEncoder /
// openOutputWithCompressMedia 初始化完成
// ============================
int FFmpeg::encodeToFile() {
    if (!outFmtCtx_.isOpened()) {
        cout << String("输出未初始化...", "Output not initialized...") << endl;
        LOGD("输出未初始化...");
        return -EINVAL;
    }

    bool videoStreamOk = venc_.isOpened() && outVStream_.isValid();
    bool audioStreamOk = aenc_.isOpened() && outAStream_.isValid();

    if (!videoStreamOk && !audioStreamOk) {
        cout << String("音视频输出流均未建立，编码取消...",
                       "No output stream created, encoding canceled...") << endl;
        LOGD("没有视频和音频流, 编码结束");
        return -EINVAL;
    }

    std::error_code ec;
    int outWidth = videoStreamOk ? venc_.width() : 0;
    int outHeight = videoStreamOk ? venc_.height() : 0;
    AVSampleFormat outSampleFmt = audioStreamOk ? aenc_.sampleFormat().get() : AV_SAMPLE_FMT_NONE;

    // 音频 FIFO 与帧大小（从已打开的编码器读取）
    AVAudioFifo *afifo = nullptr;
    int encFrameSize = 0;
    bool varFrameSize = false;
    if (audioStreamOk) {
        encFrameSize = aenc_.raw()->frame_size;
        varFrameSize = !!(aenc_.raw()->codec->capabilities & AV_CODEC_CAP_VARIABLE_FRAME_SIZE);
        if (!varFrameSize && encFrameSize > 0) {
            afifo = av_audio_fifo_alloc(outSampleFmt, 2 /*stereo*/, encFrameSize * 4);
        }
    }

    av::VideoFrame yuvFrame;
    int64_t aEncNextPts = 0;
    int64_t vEncNextPts = 0;
    int writtenVPkts = 0;
    int writtenAPkts = 0;
    if (videoStreamOk) {
        yuvFrame = av::VideoFrame(AV_PIX_FMT_YUV420P, outWidth, outHeight);
    }
    AVRational vInTimeBase = {0, 0};
    AVRational aInTimeBase = {0, 0};
    if (videoStreamOk) vInTimeBase = fmtCtx_.raw()->streams[videoStreamIndex_]->time_base;
    if (audioStreamOk) aInTimeBase = fmtCtx_.raw()->streams[audioStreamIndex_]->time_base;

    // 时间进度：总时长（微秒），未知则回退到包数进度
    int64_t totalDurationUs = fmtCtx_.raw()->duration;
    int64_t currentTimeUs = 0;
    const bool durationKnown = (totalDurationUs > 0 && totalDurationUs != AV_NOPTS_VALUE);

    cout << String("正在编码...", "Encoding...") << endl;

    // 写包 lambda
    auto writePkt = [&](av::Packet &pkt, const av::Stream &st, const AVRational &encTb,
                         int64_t frameNbSamples) -> bool {
        if (!st.isValid() || !pkt) return false;
        AVPacket *rawPkt = pkt.raw();
        rawPkt->stream_index = st.index();
        if (aenc_.isOpened() && rawPkt->duration <= 0) {
            if (frameNbSamples > 0) {
                rawPkt->duration = frameNbSamples;
            } else if (aenc_.raw()->frame_size > 0) {
                rawPkt->duration = aenc_.raw()->frame_size;
            }
        }
        // 调试：打印音频包时间戳（rescale 前后）
        bool isAudioPkt = (st.index() == outAStream_.index());
        if (isAudioPkt && writtenAPkts < 3) {
            LOGD("audio pkt pre:  pts=%lld dts=%lld dur=%lld encTb=%d/%d stTb=%d/%d",
                 (long long)rawPkt->pts, (long long)rawPkt->dts, (long long)rawPkt->duration,
                 encTb.num, encTb.den, st.raw()->time_base.num, st.raw()->time_base.den);
        }
        static int64_t audioTsOffset = AV_NOPTS_VALUE;
        if (isAudioPkt  && rawPkt->dts != AV_NOPTS_VALUE) {
            if (audioTsOffset == AV_NOPTS_VALUE && rawPkt->dts < 0) {
                audioTsOffset = -rawPkt->dts;
            }
            if (audioTsOffset > 0) {
                rawPkt->pts += audioTsOffset;
                rawPkt->dts += audioTsOffset;
            }
        }
        if (isAudioPkt && writtenAPkts < 3) {
            LOGD("audio pkt post: pts=%lld dts=%lld dur=%lld",
                 (long long)rawPkt->pts, (long long)rawPkt->dts, (long long)rawPkt->duration);
        }
        std::error_code writeEc;
        outFmtCtx_.writePacket(pkt, writeEc);
        if (writeEc) {
            LOGD("写包失败: %s", writeEc.message().c_str());
            return false;
        }
        return true;
    };

    // 视频帧编码 lambda
    auto feedVideoFrame = [&](av::VideoFrame &decFrame) {
        if (!swsCtx_ || !venc_.isOpened() || !decFrame) return;
        // 更新时间进度（取max，B帧乱序时进度不回退）
        if (durationKnown && decFrame.raw()->pts != AV_NOPTS_VALUE && vInTimeBase.num > 0) {
            int64_t frameUs = av_rescale_q(decFrame.raw()->pts, vInTimeBase, AV_TIME_BASE_Q);
            if (frameUs > currentTimeUs) currentTimeUs = frameUs;
        }
        sws_scale(swsCtx_,
                  decFrame.raw()->data, decFrame.raw()->linesize,
                  0, decFrame.height(),
                  yuvFrame.raw()->data, yuvFrame.raw()->linesize);
        // 直接使用自增帧号作为 pts：
        // 输入 time_base 分母极大(如法老.mp4 的 1/14876000)时 av_rescale_q 换算到
        // 编码器 time_base 会全部截断为 0，触发 "Invalid pts <= last" EINVAL。
        // 自增帧号在编码器 time_base 下即正确时间戳（每帧 +1），且严格单调递增。
        yuvFrame.raw()->pts = vEncNextPts;
        vEncNextPts++;
        // 调试：只打印第一次，确认帧和编码器参数匹配
        static bool vencDebugPrinted = false;
        if (!vencDebugPrinted) {
            AVFrame *yf = yuvFrame.raw();
            LOGD("encode debug: frame fmt=%d w=%d h=%d pts=%lld buf0=%p data0=%p data1=%p data2=%p ls0=%d ls1=%d | codec fmt=%d w=%d h=%d tb=%d/%d fr=%d/%d open=%d",
                 yf->format, yf->width, yf->height, (long long)yf->pts,
                 (void*)yf->buf[0], (void*)yf->data[0], (void*)yf->data[1], (void*)yf->data[2],
                 yf->linesize[0], yf->linesize[1],
                 venc_.raw()->pix_fmt, venc_.raw()->width, venc_.raw()->height,
                 venc_.raw()->time_base.num, venc_.raw()->time_base.den,
                 venc_.raw()->framerate.num, venc_.raw()->framerate.den,
                 avcodec_is_open(venc_.raw()));
            vencDebugPrinted = true;
        }
        std::error_code encEc;
        av::Packet encPkt = venc_.encode(yuvFrame, encEc);
        if (encEc) {
            LOGD("视频编码错误: %s", encEc.message().c_str());
            return;
        }
        if (writePkt(encPkt, outVStream_, venc_.raw()->time_base, 0)) {
            ++writtenVPkts;
        }
    };

    // 音频样本编码 lambda
    auto feedAudioSamples = [&](av::AudioSamples &decSamples) {
        if (!swrCtx_ || !aenc_.isOpened() || !decSamples) return;
        // 更新时间进度（与视频取max，音视频不同步时进度取较大者）
        if (durationKnown && decSamples.raw()->pts != AV_NOPTS_VALUE && aInTimeBase.num > 0) {
            int64_t samplesUs = av_rescale_q(decSamples.raw()->pts, aInTimeBase, AV_TIME_BASE_Q);
            if (samplesUs > currentTimeUs) currentTimeUs = samplesUs;
        }
        int dstNbSamples = swr_get_out_samples(swrCtx_, decSamples.samplesCount());
        if (dstNbSamples <= 0) return;
        av::AudioSamples encSamples(outSampleFmt, dstNbSamples,
                                    AV_CH_LAYOUT_STEREO, adec_.sampleRate());
        int swrRet = swr_convert(swrCtx_,
                                 encSamples.raw()->data, dstNbSamples,
                                 (const uint8_t **) decSamples.raw()->data,
                                 decSamples.samplesCount());
        if (swrRet < 0) return;
        if (afifo) {
            av_audio_fifo_write(afifo, (void **) encSamples.raw()->data, swrRet);
            while (av_audio_fifo_size(afifo) >= encFrameSize) {
                av::AudioSamples encFrame(outSampleFmt, encFrameSize,
                                          AV_CH_LAYOUT_STEREO, adec_.sampleRate());
                av_audio_fifo_read(afifo, (void **) encFrame.raw()->data, encFrameSize);
                encFrame.raw()->pts = aEncNextPts;
                aEncNextPts += encFrameSize;
                std::error_code encEc;
                av::Packet encPkt = aenc_.encode(encFrame, encEc);
                if (encEc) {
                    LOGD("音频编码错误: %s", encEc.message().c_str());
                    return;
                }
                if (writePkt(encPkt, outAStream_, aenc_.raw()->time_base, encFrameSize)) {
                    ++writtenAPkts;
                }
            }
        } else {
            encSamples.raw()->nb_samples = swrRet;
            encSamples.raw()->pts = aEncNextPts;
            int64_t nbSmpl = swrRet;
            aEncNextPts += nbSmpl;
            std::error_code encEc;
            av::Packet encPkt = aenc_.encode(encSamples, encEc);
            if (encEc) {
                LOGD("音频编码错误: %s", encEc.message().c_str());
                return;
            }
            if (writePkt(encPkt, outAStream_, aenc_.raw()->time_base, nbSmpl)) {
                ++writtenAPkts;
            }
        }
    };

    // ========== 主循环：读包 → 解码 → 编码 ==========
    int processedPkts = 0;
    while (true) {
        std::error_code readEc;
        av::Packet pkt = fmtCtx_.readPacket(readEc);
        if (readEc) { LOGD("读包结束/出错: %s", readEc.message().c_str()); break; }
        if (!pkt) break;
        bool isVideo = (videoStreamOk && pkt.streamIndex() == videoStreamIndex_);
        bool isAudio = (audioStreamOk && pkt.streamIndex() == audioStreamIndex_);
        if (!isVideo && !isAudio) continue;
        if ((++processedPkts % 200) == 0) {
            if (durationKnown) {
                int percent = (int)(currentTimeUs * 100 / totalDurationUs);
                if (percent > 100) percent = 100;
                cout << String("进度: ", "Progress: ") << percent << "%  ("
                     << currentTimeUs / 1000000 << "s / " << totalDurationUs / 1000000 << "s)"
                     << endl;
            } else {
                cout << String("进度: 已处理 ", "Progress: processed ") << processedPkts
                     << String(" 包", " packets") << endl;
            }
        }
        if (isVideo && venc_.isOpened()) {
            std::error_code decEc;
            av::VideoFrame decFrame = vdec_.decode(pkt, decEc);
            if (decEc) { LOGD("视频解码错误: %s", decEc.message().c_str()); continue; }
            if (decFrame) feedVideoFrame(decFrame);
        }
        if (isAudio && swrCtx_) {
            std::error_code decEc;
            av::AudioSamples decSamples = adec_.decode(pkt, decEc);
            if (decEc) { LOGD("音频解码错误: %s", decEc.message().c_str()); continue; }
            if (decSamples) feedAudioSamples(decSamples);
        }
    }

    // ========== 冲刷解码器 ==========
    if (videoStreamOk && swsCtx_) {
        avcodec_send_packet(vdec_.raw(), nullptr);
        while (true) {
            std::error_code decEc;
            av::VideoFrame decFrame = vdec_.decode(av::Packet(), decEc);
            if (decEc || !decFrame) break;
            feedVideoFrame(decFrame);
        }
    }
    if (audioStreamOk && swrCtx_) {
        avcodec_send_packet(adec_.raw(), nullptr);
        while (true) {
            std::error_code decEc;
            av::AudioSamples decSamples = adec_.decode(av::Packet(), decEc);
            if (decEc || !decSamples) break;
            feedAudioSamples(decSamples);
        }
    }

    // ========== 冲刷 swr 缓冲与音频 FIFO ==========
    if (audioStreamOk && swrCtx_) {
        while (true) {
            int avail = swr_get_out_samples(swrCtx_, 0);
            if (avail <= 0) break;
            av::AudioSamples encSamples(outSampleFmt, avail,
                                        AV_CH_LAYOUT_STEREO, adec_.sampleRate());
            int swrRet = swr_convert(swrCtx_, encSamples.raw()->data, avail, nullptr, 0);
            if (swrRet <= 0) break;
            if (afifo) {
                av_audio_fifo_write(afifo, (void **) encSamples.raw()->data, swrRet);
            } else {
                encSamples.raw()->nb_samples = swrRet;
                encSamples.raw()->pts = aEncNextPts;
                int64_t nbSmpl = swrRet;
                aEncNextPts += nbSmpl;
                std::error_code encEc;
                av::Packet encPkt = aenc_.encode(encSamples, encEc);
                if (writePkt(encPkt, outAStream_, aenc_.raw()->time_base, nbSmpl)) {
                    ++writtenAPkts;
                }
            }
        }
        if (afifo) {
            while (av_audio_fifo_size(afifo) > 0) {
                int fifoSize = av_audio_fifo_size(afifo);
                int n = (fifoSize < encFrameSize) ? fifoSize : encFrameSize;
                av::AudioSamples encFrame(outSampleFmt, n,
                                          AV_CH_LAYOUT_STEREO, adec_.sampleRate());
                av_audio_fifo_read(afifo, (void **) encFrame.raw()->data, n);
                encFrame.raw()->pts = aEncNextPts;
                aEncNextPts += n;
                std::error_code encEc;
                av::Packet encPkt = aenc_.encode(encFrame, encEc);
                if (writePkt(encPkt, outAStream_, aenc_.raw()->time_base, n)) {
                    ++writtenAPkts;
                }
            }
        }
    }

    // ========== 冲刷编码器 ==========
    if (videoStreamOk) {
        int guard = 0;
        while (guard++ < 100000) {
            std::error_code encEc;
            av::Packet encPkt = venc_.encode(encEc);
            if (encEc) break;
            if (!encPkt) break;
            if (writePkt(encPkt, outVStream_, venc_.raw()->time_base, 0)) {
                ++writtenVPkts;
            }
        }
    }
    if (audioStreamOk) {
        int guard = 0;
        while (guard++ < 100000) {
            std::error_code encEc;
            av::Packet encPkt = aenc_.encode(encEc);
            if (encEc) break;
            if (!encPkt) break;
            if (writePkt(encPkt, outAStream_, aenc_.raw()->time_base,
                          aenc_.raw()->frame_size)) {
                ++writtenAPkts;
            }
        }
    }

    // 编码完成，进度打满
    if (durationKnown) {
        cout << String("进度: 100%", "Progress: 100%") << endl;
    }

    // ========== 写文件尾 ==========
    outFmtCtx_.writeTrailer(ec);
    // 兜底修正音频流 duration
    if (outAStream_.isValid() && aenc_.isOpened()) {
        outAStream_.raw()->duration = av_rescale_q(aEncNextPts, {1, adec_.sampleRate()},
                                                     outAStream_.raw()->time_base);
    }
    if (ec) {
        cout << String("写文件尾失败: ", "Fail to write trailer: ") << ec.message() << endl;
    }

    cout << String("编码完成！写出视频包: ", "Encoding finished! Video packets written: ")
         << writtenVPkts
         << String(", 音频包: ", ", audio packets written: ") << writtenAPkts << endl;
    LOGD("编码完成: 输入包=%d, 视频包=%d, 音频包=%d",
         processedPkts, writtenVPkts, writtenAPkts);

    if (afifo) av_audio_fifo_free(afifo);
    return 0;
}

// ============================
// 便捷重载：用指定名称的编码器初始化输出并编码写出文件
// 内部等价于：name→codec_id → openOutPutWithEncoder() → encodeToFile()
// ============================
int FFmpeg::encodeToFile(const char* outputPath,
                         const char* videoEncoderName,
                         const char* audioEncoderName) {
    if (!fmtCtx_.isOpened() || !outputPath || !outputPath[0]) {
        return -EINVAL;
    }

    bool wantVideo = videoEncoderName && videoEncoderName[0] && hasVideo();
    bool wantAudio = audioEncoderName && audioEncoderName[0] && hasAudio();
    if (!wantVideo && !wantAudio) {
        cout << String("没有可编码的音视频流...", "No audio/video stream to encode...") << endl;
        return -EINVAL;
    }

    LOGD("开始编码: video=%s audio=%s",
         wantVideo ? videoEncoderName : "(none)",
         wantAudio ? audioEncoderName : "(none)");
    cout << "===== 开始编码 =====" << endl;
    cout << String("输出: ", "Output: ") << outputPath << endl;

    // 根据编码器名称查找 codec_id
    AVCodecID videoID = AV_CODEC_ID_NONE;
    AVCodecID audioID = AV_CODEC_ID_NONE;
    if (wantVideo) {
        const AVCodec *vCodec = avcodec_find_encoder_by_name(videoEncoderName);
        if (!vCodec) {
            cout << String("找不到视频编码器: ", "Can not find video encoder: ")
                 << videoEncoderName << endl;
            return -EINVAL;
        }
        videoID = vCodec->id;
    }
    if (wantAudio) {
        const AVCodec *aCodec = avcodec_find_encoder_by_name(audioEncoderName);
        if (!aCodec) {
            cout << String("找不到音频编码器: ", "Can not find audio encoder: ")
                 << audioEncoderName << endl;
            return -EINVAL;
        }
        audioID = aCodec->id;
    }

    // 设置输出路径
    outPath_ = outputPath;

    // 输出初始化（打开输出 + 创建编码器 + 创建流 + 写文件头）
    int ret = openOutPutWithEncoder(videoID, audioID);
    if (ret != 0) {
        return ret;
    }

    // 执行编码循环（调用无参可复用版本）
    return encodeToFile();
}

// ============================
// JNI 导出函数
// ============================

// 读取 nativeOpenFile 的探测结果（与全局 ffmpeg 实例状态无关，
// 即使 cppMain 正在编码也能正确返回当前选中文件的流信息）
extern "C" JNIEXPORT jboolean JNICALL
Java_com_kgmdecoder_app_Selecting_hasVideo(JNIEnv *env, jobject thiz) {
    return g_fileHasVideo.load() ? JNI_TRUE : JNI_FALSE;
}

extern "C" JNIEXPORT jboolean JNICALL
Java_com_kgmdecoder_app_Selecting_hasAudio(JNIEnv *env, jobject thiz) {
    return g_fileHasAudio.load() ? JNI_TRUE : JNI_FALSE;
}

extern "C" JNIEXPORT void JNICALL
Java_com_kgmdecoder_app_MainActivity_releaseFFmpeg(JNIEnv *env, jobject thiz) {
    releaseGlobalFFmpeg();
}

void releaseGlobalFFmpeg() {
    if (ffmpeg) {
        delete ffmpeg;
        ffmpeg = nullptr;
    }
}
