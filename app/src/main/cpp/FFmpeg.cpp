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

#include <cmath>
#include <algorithm>
#include "functions.h"

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
        : cout(os), cin(is), inited(true), outPath_(g_downloadDir + "/default.mp4"), encodeThreads(2) {
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
    encodeThreads = 2;
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

    outPath_ = g_downloadDir + "/default.mp4";
    url_.clear();
}

// ============================
// 打开输入媒体文件
// ============================
int FFmpeg::openInput(const char* url) {

    auto getEncodeThreads = []() -> uint8_t {
        auto checkFile = []() -> bool {
            if (!std::filesystem::exists("/data/user/0/com.kgmdecoder.app/files/config.dat")) {
                std::ofstream f("/data/user/0/com.kgmdecoder.app/files/config.dat");
                f << "0" << std::endl;
                f << "2" << std::endl;
                return false;
            }
            return true;
        };
        if (!checkFile()) {
            return 2;
        }
        std::fstream f("/data/user/0/com.kgmdecoder.app/files/config.dat");
        std::string line;
        std::getline(f, line);
        std::getline(f, line);
        if (line.empty()) return 2;
        std::string target = "";
        for (size_t i = 0; i < line.size(); i++) {
            if ('0' <= line[i] && line[i] <= '9') target.push_back(line[i]);
            else return 2;
        }
        if (target.empty()) return 2;
        int c = std::stoi(target);
        if (c < 1) return 2;
        return c <= av_cpu_count() ? (uint8_t)c : (uint8_t)av_cpu_count();
    };

    if (!url || !url[0]) {
        return -EINVAL;
    }

    close();

    encodeThreads = getEncodeThreads();

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
            vdec_.raw()->thread_count = encodeThreads > 0 ? encodeThreads : 2;
            vdec_.raw()->thread_type  = FF_THREAD_FRAME;
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

// ==========================================
// 辅助：根据编码器支持的采样格式自动挑选输出采样格式
// ==========================================
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

// ==================================
// 用指定编码器ID完成输出初始化（公开入口）
// ==================================
int FFmpeg::openOutPutWithEncoder(AVCodecID videoID, AVCodecID audioID,
                                   int64_t videoBitrate,
                                   const char* presetStr,
                                   double targetFps,
                                   int64_t audioBitrate,
                                   int outWidth,
                                   int outHeight,
                                   int targetSampleRate,
                                   bool allowHardware) {
    return initOutputContext(videoID, audioID, videoBitrate, presetStr, targetFps, audioBitrate,
                             outWidth, outHeight, targetSampleRate, allowHardware);
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
                               int64_t audioBitrate,
                               int outWidth,
                               int outHeight,
                               int targetSampleRate,
                               bool allowHardware,
                               bool keepSourceChannelLayout) {
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
        // 尝试启用Android硬件编码（allowHardware=false 时强制软件编码）
        const AVCodec* hwCodec = nullptr;
        if (allowHardware) {
            if (videoID == AV_CODEC_ID_H264) hwCodec = avcodec_find_encoder_by_name("h264_mediacodec");
            else if (videoID == AV_CODEC_ID_H265) hwCodec = avcodec_find_encoder_by_name("hevc_mediacodec");
        }

        av::Codec vCodec = hwCodec ? av::Codec(hwCodec) : av::findEncodingCodec(videoID);
        const bool isHardWare = (hwCodec != nullptr);
        if (vCodec.isNull()) {
            cout << String("找不到视频编码器: ", "Can not find video encoder: ") << videoID << endl;
            LOGD("找不到视频编码器: %s", avcodec_get_name(videoID));
            return -EINVAL;
        }


        venc_ = av::VideoEncoderContext(vCodec);

        int srcWidth = vdec_.width() & ~1;
        int srcHeight = vdec_.height() & ~1;
        if (outWidth <= 0) outWidth = srcWidth;
        if (outHeight <= 0) outHeight = srcHeight;
        outWidth &= ~1;
        outHeight &= ~1;
        venc_.raw()->width = outWidth;
        venc_.raw()->height = outHeight;
        // 编码器输入格式必须与 sws 输出/yuvFrame 一致（都是 YUV420P）。
        // 不能用 vdec_.pixelFormat()：硬件解码输出 NV12、10bit 源输出 P010/YUV420P10
        // 时，编码器按错误格式解释 UV 平面会导致颜色糊在一起花屏。
        const AVPixelFormat *supportedPixelFmts = venc_.raw()->codec->pix_fmts;
        if (supportedPixelFmts) {
            // 使用于源文件最相似的
            venc_.raw()->pix_fmt = avcodec_find_best_pix_fmt_of_list(
                    supportedPixelFmts, vdec_.pixelFormat(), 0, nullptr
            );
            if (venc_.raw()->pix_fmt == AV_PIX_FMT_NONE)  {venc_.raw()->pix_fmt = supportedPixelFmts[0]; }
        } else {
            // 兜底用
            venc_.raw()->pix_fmt = AV_PIX_FMT_YUV420P;
        }

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
            int threads = encodeThreads > 0 ? encodeThreads : 2;
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
            venc_.raw()->thread_count = encodeThreads > 0 ? encodeThreads : 2;
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
        venc_.raw()->gop_size = 25;
        venc_.raw()->max_b_frames = 0;
        venc_.raw()->qmax = 30;
        venc_.raw()->qmin = 2;

        // 码率与 preset 设置
        // 默认 preset 用 veryfast（重编码提速约 50%，画质损失可接受）
        const char *effectivePreset = (presetStr && presetStr[0]) ? presetStr : "veryfast";
        if (videoBitrate > 0) {
            venc_.raw()->bit_rate = videoBitrate;
            // 外部传入 bitrate 时优先使用 ABR，不使用 CRF
            venc_.setOption("preset", effectivePreset);
        } else if (videoID == AV_CODEC_ID_H264 || videoID == AV_CODEC_ID_H265) {
            // 未传 bitrate 时回退到默认 preset+crf
            venc_.setOption("preset", effectivePreset);
            venc_.setOption("tune", "zerolatency");
            venc_.setOption("crf", "27");
        } else {
            venc_.raw()->bit_rate = 2000000;
        }

        // mpeg4 等编码器需要 GLOBAL_HEADER
        venc_.raw()->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;

        // libx264 专用提速参数（重编码场景，画质优先让位于速度）
        // 仅对 libx264 有效，硬编/其他编码器忽略
        if (!isHardWare && vCodec.name() == "libx264") {
            av_opt_set(venc_.raw()->priv_data, "subme", "2", 0);        // 默认7, 降到2提速明显
            av_opt_set(venc_.raw()->priv_data, "refs", "1", 0);          // 默认3, 降到1
            av_opt_set(venc_.raw()->priv_data, "rc-lookahead", "10", 0); // 默认40, 降到10
            av_opt_set(venc_.raw()->priv_data, "aq-mode", "0", 0);        // 默认1, 关闭自适应量化
            av_opt_set(venc_.raw()->priv_data, "scenecut", "0", 0);       // 关闭场景切换检测(减少I帧波动)
            LOGD("libx264 提速参数: subme=2 refs=1 rc-lookahead=10 aq-mode=0 scenecut=0");
        }

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
        // 显式将输出流的 time_base 与编码器对齐：avformat_new_stream 只给出默认值，
        // 若其 num 非 0，muxer 的 init_muxer 不会再用 sample_rate 推导 time_base，
        // 导致写入时把编码器时基(如 1/48000)的 pts 错误换算，Ogg 的 granule 失真 -> 播放加速。
        outVStream_.raw()->time_base = venc_.raw()->time_base;

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
            int outSampleRate = (targetSampleRate > 0) ? targetSampleRate : adec_.sampleRate();
            // Opus 只支持 8/12/16/24/48 kHz。若仍按源采样率(常见 44.1kHz)交给编码器，
            // 编码器会按 48kHz 处理，而重采样器仍输出源采样率，时间戳推进过快 → 播放加速。
            if (audioID == AV_CODEC_ID_OPUS &&
                outSampleRate != 8000 && outSampleRate != 12000 && outSampleRate != 16000 &&
                outSampleRate != 24000 && outSampleRate != 48000) {
                outSampleRate = (outSampleRate >= 36000) ? 48000 :
                                (outSampleRate >= 20000) ? 24000 :
                                (outSampleRate >= 14000) ? 16000 :
                                (outSampleRate >= 10000) ? 12000 : 8000;
                LOGD("Opus 不支持源采样率，已重采样为 %d Hz", outSampleRate);
            }
            aenc_.setSampleRate(outSampleRate);
            aenc_.setSampleFormat(outSampleFmt);
            aenc_.setBitRate(audioBitrate > 0 ? audioBitrate : 128 * 1000);
            aenc_.setTimeBase(av::Rational(1, outSampleRate));

            if (keepSourceChannelLayout) {
                // 沿用源声道布局（单声道/立体声/5.1 等保持不变）
                av_channel_layout_copy(&aenc_.raw()->ch_layout, &adec_.raw()->ch_layout);
            } else {
                AVChannelLayout stereoLayout = AV_CHANNEL_LAYOUT_STEREO;
                av_channel_layout_copy(&aenc_.raw()->ch_layout, &stereoLayout);
            }

            if (needGlobalHeader) {
                aenc_.raw()->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
            }

            // 音频编码提速：libmp3lame/FLAC 调低压缩级别（0=最快, 默认5）
            if (aCodec.name() == "libmp3lame" || aCodec.name() == "flac") {
                av_opt_set_int(aenc_.raw()->priv_data, "compression_level", 2, 0);
                LOGD("音频编码器 %s 压缩级别设为 2(提速)", aCodec.name());
            }

            // FFmpeg 内置的 opus/vorbis 原生编码器被标记为 experimental（本工程未编译 libopus/libvorbis），
            // 未开启严格合规时 avcodec_open2 会直接报 "Experimental feature" 并打开失败，
            // 导致 ogg 输出没有任何音频流，随后写入文件头时报 "Invalid argument"。
            if (aCodec.raw()->capabilities & AV_CODEC_CAP_EXPERIMENTAL) {
                aenc_.raw()->strict_std_compliance = FF_COMPLIANCE_EXPERIMENTAL;
                LOGD("音频编码器 %s 为实验性编码器，已启用 experimental 合规", aCodec.name());
            }

            aenc_.open(ec);
            if (ec) {
                cout << String("打开音频编码器失败: ", "Fail to open audio encoder: ")
                     << ec.message() << endl;
                LOGD("打开音频编码器%s失败: %s", avcodec_get_name(aCodec.id()), ec.message().c_str());
                aenc_ = av::AudioEncoderContext();
            } else {
                // FFmpeg 8.x 的原生 opus 编码器把 avctx->frame_size 设为由 opus_delay 推导出的块大小
                // （默认 360ms → 960 样本），但其心理声学模块(enc_psy)仍按 2.5ms(=120 样本)的步长硬编码推进：
                // 每产出 1 个包只回退 steps_out=块大小/120=8 步，且每包只从队列取走 1 个块(960 样本)。
                // 若按上报的 frame_size=960 喂 960 样本帧，则每 8 次调用才出 1 包，输入(8×960)远大于
                // 输出(960)，导致约 8× 丢帧 → ogg 播放加速、内部 bufqueue 溢出丢帧、尾部冲刷极慢(进度卡 99%)。
                // 修复：把 opus 的 frame_size 对齐到 2.5ms 步长(48kHz→120)，按 120 样本/帧喂入，
                // 使“8 次调用 × 120 样本 = 960 样本 = 每包 960 样本”收支平衡（即 FFmpeg ≤7.1 的原始约定）。
                if (aCodec.id() == AV_CODEC_ID_OPUS) {
                    int psyStep = aenc_.raw()->sample_rate > 0 ? aenc_.raw()->sample_rate / 400 : 120;
                    if (psyStep > 0 && aenc_.raw()->frame_size != psyStep) {
                        aenc_.raw()->frame_size = psyStep;
                    }
                }
                // Opus 原生编码器可能不设置 initial_padding(pre-skip)，
                // 导致 ogg muxer 计算 granulepos 错误，播放时音频加速/空白。
                // 标准 Opus 48kHz pre-skip 为 312 样本(6.5ms)。
                if (aCodec.id() == AV_CODEC_ID_OPUS && aenc_.raw()->initial_padding == 0) {
                    aenc_.raw()->initial_padding = 312;
                }
                outAStream_ = outFmtCtx_.addStream(aenc_, ec);
                // 关键：把输出音频流的 time_base 显式设为编码器时基(Opus 为 1/48000)。
                // Ogg muxer 用流 time_base 把 pkt->pts 换算成 granule；若二者不一致，
                // 时间戳会被错误换算，播放时音频加速。
                if (outAStream_.isValid()) {
                    outAStream_.raw()->time_base = aenc_.raw()->time_base;
                }
                // 复制 bit_rate 到 codecpar
                if (outAStream_.isValid()) {
                    outAStream_.raw()->codecpar->bit_rate = aenc_.raw()->bit_rate;
                }
                // 关键：手动确保 initial_padding（Opus pre-skip）和 extradata（OpusHead）
                // 复制到 codecpar。ogg muxer 用 initial_padding 算 granulepos，用 extradata
                // 识别 opus 流。avcpp addStream 可能没复制全，这里手动补一次。
                if (outAStream_.isValid()) {
                    outAStream_.raw()->codecpar->initial_padding = aenc_.raw()->initial_padding;
                    if (aenc_.raw()->extradata && aenc_.raw()->extradata_size > 0) {
                        av_free(outAStream_.raw()->codecpar->extradata);
                        outAStream_.raw()->codecpar->extradata = (uint8_t*) av_mallocz(aenc_.raw()->extradata_size + AV_INPUT_BUFFER_PADDING_SIZE);
                        memcpy(outAStream_.raw()->codecpar->extradata, aenc_.raw()->extradata, aenc_.raw()->extradata_size);
                        outAStream_.raw()->codecpar->extradata_size = aenc_.raw()->extradata_size;
                    }
                }
                if (ec) {
                    cout << String("创建输出音频流失败: ",
                                   "Fail to create output audio stream: ") << ec.message() << endl;
                    LOGD("创建输出音频流失败: %s", ec.message().c_str());
                    aenc_ = av::AudioEncoderContext();
                } else {
                    AVChannelLayout outLayout;
                    if (keepSourceChannelLayout) {
                        av_channel_layout_copy(&outLayout, &adec_.raw()->ch_layout);
                    } else {
                        outLayout = AV_CHANNEL_LAYOUT_STEREO;
                    }
                    AVChannelLayout inLayout = {};
                    av_channel_layout_copy(&inLayout, &adec_.raw()->ch_layout);

                    int swrRet = swr_alloc_set_opts2(&swrCtx_,
                                                     &outLayout, outSampleFmt, outSampleRate,
                                                     &inLayout, adec_.sampleFormat(),
                                                     adec_.sampleRate(), 0, nullptr
                    );
                    av_channel_layout_uninit(&inLayout);
                    if (keepSourceChannelLayout) av_channel_layout_uninit(&outLayout);
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

    {
        // 读取配置文件第3行 comment 键值对，写入输出文件 metadata
        // 空值不写入（av_dict_set 传 nullptr 会删除已有条目，空字符串则会写入空值）
        std::ifstream cfg("/data/user/0/com.kgmdecoder.app/files/config.dat");
        if (cfg.is_open()) {
            std::string line;
            std::getline(cfg, line); // 第1行：experimentalFunction
            std::getline(cfg, line); // 第2行：encodeThreads
            std::getline(cfg, line); // 第3行：comment
            if (!line.empty()) {
                av_dict_set(&outFmtCtx_.raw()->metadata, "comment", line.c_str(), 0);
            }
        }

        // 读取源文件的信息
        const AVDictionary *dict  = fmtCtx_.raw()->metadata;
        const AVDictionaryEntry *entry = nullptr;
        while ((entry = av_dict_iterate(dict, entry))) {
            LOGI("字典键值%s正在迭代中...", entry->key != nullptr ? entry->key : "<NONE>");
            if (entry && entry->value) av_dict_set(&outFmtCtx_.raw()->metadata, entry->key, entry->value, 0);
        }
    }

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

    if (hasVideo()) LOGI("视频码率: %li", venc_.bitRate());
    if (hasAudio()) {
        LOGI("音频码率: %li", aenc_.bitRate());
        LOGD("AFTER writeHeader: outAStream timeBase=%d/%d encTimeBase=%d/%d",
             outAStream_.raw()->time_base.num, outAStream_.raw()->time_base.den,
             aenc_.raw()->time_base.num, aenc_.raw()->time_base.den);
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

    AVDictionaryEntry* entry = nullptr;
    entry = av_dict_get(rawCtx->metadata, "comment", nullptr, 0);
    if (entry && entry->value)
        cout << String("介绍: ", "Comment: ") << entry->value << endl;

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

        entry = nullptr;
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

    // preset 固定 veryfast（只控制编码速度，不参与压缩等级控制）
    const std::string preset = "veryfast";

    // 压缩等级通过 码率/帧率/分辨率/采样率 控制：level=1 最小体积, level=10 原画质
    // 码率缩放: 0.15 ~ 1.0
    const double bitrateScale = 0.15 + (presetLevel - 1) * 0.85 / 9.0;
    // 帧率缩放: 0.4 ~ 1.0（最低不低于 15fps）
    const double fpsScale = 0.4 + (presetLevel - 1) * 0.6 / 9.0;
    // 分辨率缩放: 0.6 ~ 1.0
    const double scaleFactor = 0.6 + (presetLevel - 1) * 0.4 / 9.0;
    // 采样率档位: 22050 ~ 48000
    const int sampleRateTable[] = {22050, 24000, 32000, 44100, 48000};
    const int srIdx = (presetLevel - 1) * 4 / 9;  // 0~4
    const int targetSampleRate = sampleRateTable[srIdx];

    // 读取输入流原始参数（压缩强制用 H.264 软件编码，画质优于硬件编码）
    AVCodecID videoID = AV_CODEC_ID_NONE;
    AVCodecID audioID = AV_CODEC_ID_NONE;
    int64_t srcVideoBitrate = 0;
    int64_t srcAudioBitrate = 0;
    int srcWidth = 0, srcHeight = 0;
    int srcSampleRate = 0;
    double srcVideoFps = 25.0;

    if (hasVideo()) {
        AVStream* st = fmtCtx_.raw()->streams[videoStreamIndex_];
        videoID = AV_CODEC_ID_H264;  // 强制 H.264，配合 allowHardware=false 走 libx264 软编
        srcVideoBitrate = st->codecpar->bit_rate;
        if (srcVideoBitrate <= 0) srcVideoBitrate = 2000000;
        srcWidth = st->codecpar->width;
        srcHeight = st->codecpar->height;
        AVRational fr = st->avg_frame_rate;
        if (fr.num > 0 && fr.den > 0) srcVideoFps = av_q2d(fr);
    }
    if (hasAudio()) {
        AVStream* st = fmtCtx_.raw()->streams[audioStreamIndex_];
        audioID = st->codecpar->codec_id;
        srcAudioBitrate = st->codecpar->bit_rate;
        if (srcAudioBitrate <= 0) srcAudioBitrate = 128000;
        srcSampleRate = st->codecpar->sample_rate;
    }

    // 计算输出分辨率（保证偶数）
    int outWidth = 0, outHeight = 0;
    if (hasVideo()) {
        outWidth = (int)(srcWidth * scaleFactor / 2) * 2;
        outHeight = (int)(srcHeight * scaleFactor / 2) * 2;
        if (outWidth < 16) outWidth = 16;
        if (outHeight < 16) outHeight = 16;
    }

    // 计算目标帧率（最低 15fps，不超过源帧率）
    double targetFps = srcVideoFps * fpsScale;
    if (targetFps < 15.0) targetFps = 15.0;
    if (targetFps > srcVideoFps) targetFps = srcVideoFps;

    // 码率按等级缩放，音频再叠加采样率比例
    int64_t outVideoBitrate = (int64_t)(srcVideoBitrate * bitrateScale);
    int64_t outAudioBitrate = (int64_t)(srcAudioBitrate * bitrateScale);
    if (hasAudio() && srcSampleRate > 0 && targetSampleRate < srcSampleRate) {
        outAudioBitrate = (int64_t)(outAudioBitrate * (double)targetSampleRate / srcSampleRate);
    }

    // x264/x265/vp9 支持 preset 私有选项
    bool codecSupportPreset = false;
    if (hasVideo()) {
        if (videoID == AV_CODEC_ID_H264 || videoID == AV_CODEC_ID_H265 ||
            videoID == AV_CODEC_ID_VP9) {
            codecSupportPreset = true;
        }
    }

    // 设置输出路径
    outPath_ = outputPath;

    cout << "===== 开始压缩 =====" << endl;
    cout << String("输出: ", "Output: ") << outputPath << endl;
    cout << String("压缩等级: ", "Compress level: ") << presetLevel << endl;
    cout << String("  码率缩放: ", "  Bitrate scale: ") << bitrateScale << endl;
    cout << String("  帧率缩放: ", "  FPS scale: ") << fpsScale << " (" << targetFps << " fps)" << endl;
    cout << String("  分辨率缩放: ", "  Resolution scale: ") << scaleFactor << endl;
    if (hasVideo()) {
        cout << String("输出分辨率: ", "Output resolution: ") << outWidth << "x" << outHeight << endl;
        cout << String("视频码率: ", "Video bitrate: ") << (outVideoBitrate / 1000) << " kbps" << endl;
    }
    if (hasAudio()) {
        cout << String("输出采样率: ", "Output sample rate: ") << targetSampleRate << " Hz" << endl;
        cout << String("音频码率: ", "Audio bitrate: ") << (outAudioBitrate / 1000) << " kbps" << endl;
    }
    cout.flush();

    // 输出初始化（直接调用私有 initOutputContext，与 openOutPutWithEncoder 独立，互不调用）
    // allowHardware=false：压缩强制 libx264 软件编码，避免硬件编码画质差
    int ret = initOutputContext(
            videoID, audioID,
            outVideoBitrate,
            codecSupportPreset ? preset.c_str() : nullptr,
            targetFps,
            outAudioBitrate,
            outWidth, outHeight,
            targetSampleRate,
            false  // allowHardware=false
    );
    if (ret != 0) {
        return ret;
    }

    // 仅完成输出初始化，编码由调用方后续调用 encodeToFile() 执行
    return 0;
}

// =============================================================
// 直接复用源文件所有解码器参数初始化输出
// 分辨率/帧率/码率/采样率/声道布局均不变，仅编码线程数由 config.dat 控制
// =============================================================
int FFmpeg::openOutputWithSourceParams(const char* outputPath) {
    if (!fmtCtx_.isOpened()) {
        cout << String("未打开输入文件...", "Didn't open input file...") << endl;
        return -EINVAL;
    }
    if (!hasVideo() && !hasAudio()) {
        cout << String("未找到音视频流...", "Can not find video/audio stream...") << endl;
        return -EINVAL;
    }

    outPath_ = outputPath;

    // 直接从源流 codecpar 提取原始编码器 ID 和码率，不做任何缩放/修改
    AVCodecID videoID = AV_CODEC_ID_NONE;
    AVCodecID audioID = AV_CODEC_ID_NONE;
    int64_t srcVideoBitrate = 0;
    int64_t srcAudioBitrate = 0;

    if (hasVideo()) {
        AVStream* st = fmtCtx_.raw()->streams[videoStreamIndex_];
        videoID = st->codecpar->codec_id;
        srcVideoBitrate = st->codecpar->bit_rate;
    }
    if (hasAudio()) {
        AVStream* st = fmtCtx_.raw()->streams[audioStreamIndex_];
        audioID = st->codecpar->codec_id;
        srcAudioBitrate = st->codecpar->bit_rate;
    }

    // 容器兼容性检查 + 自动降级：输出容器不支持源编码器时，自动选择容器支持的默认编码器
    const AVOutputFormat *ofmt = av_guess_format(nullptr, outputPath, nullptr);
    if (ofmt) {
        if (hasVideo() && avformat_query_codec(ofmt, videoID, FF_COMPLIANCE_NORMAL) == 0) {
            cout << String("容器不支持视频编码器 ", "Container doesn't support video encoder ")
                 << avcodec_get_name(videoID)
                 << String("，自动降级为 libx264", ", auto fallback to libx264") << endl;
            LOGD("容器不支持视频编码器 %s, 自动降级为 libx264", avcodec_get_name(videoID));
            videoID = AV_CODEC_ID_H264;
            srcVideoBitrate = 0;  // 降级后用编码器默认码率
        }
        if (hasAudio() && avformat_query_codec(ofmt, audioID, FF_COMPLIANCE_NORMAL) == 0) {
            // 根据容器选择兼容的默认音频编码器
            AVCodecID fallbackAudioID = AV_CODEC_ID_MP3;  // 默认 libmp3lame，兼容性最好
            const char *ofmtName = ofmt->name;
            if (strcmp(ofmtName, "wav") == 0) {
                fallbackAudioID = AV_CODEC_ID_PCM_S16LE;
            } else if (strcmp(ofmtName, "mp3") == 0) {
                fallbackAudioID = AV_CODEC_ID_MP3;
            } else if (strcmp(ofmtName, "mp4") == 0 || strcmp(ofmtName, "mov") == 0 ||
                       strcmp(ofmtName, "m4a") == 0 || strcmp(ofmtName, "aac") == 0) {
                fallbackAudioID = AV_CODEC_ID_AAC;
            } else if (strcmp(ofmtName, "ogg") == 0 || strcmp(ofmtName, "oga") == 0) {
                fallbackAudioID = AV_CODEC_ID_VORBIS;
            }
            // 确认降级后的编码器确实被容器支持
            if (avformat_query_codec(ofmt, fallbackAudioID, FF_COMPLIANCE_NORMAL) == 0) {
                fallbackAudioID = AV_CODEC_ID_MP3;  // 兜底再试 MP3
            }
            cout << String("容器不支持音频编码器 ", "Container doesn't support audio encoder ")
                 << avcodec_get_name(audioID)
                 << String("，自动降级为 ", ", auto fallback to ")
                 << avcodec_get_name(fallbackAudioID) << endl;
            LOGD("容器不支持音频编码器 %s, 自动降级为 %s",
                 avcodec_get_name(audioID), avcodec_get_name(fallbackAudioID));
            audioID = fallbackAudioID;
            srcAudioBitrate = 0;  // 降级后用编码器默认码率
        }
    }

    cout << "===== 直接转码（沿用源参数） =====" << endl;
    cout << String("输出: ", "Output: ") << outputPath << endl;
    if (hasVideo()) {
        cout << String("视频编码器: ", "Video encoder: ") << avcodec_get_name(videoID)
             << "  " << vdec_.width() << "x" << vdec_.height() << endl;
        if (srcVideoBitrate > 0)
            cout << String("视频码率: ", "Video bitrate: ") << (srcVideoBitrate / 1000) << " kbps" << endl;
    }
    if (hasAudio()) {
        cout << String("音频编码器: ", "Audio encoder: ") << avcodec_get_name(audioID)
             << "  " << adec_.sampleRate() << " Hz" << endl;
        if (srcAudioBitrate > 0)
            cout << String("音频码率: ", "Audio bitrate: ") << (srcAudioBitrate / 1000) << " kbps" << endl;
    }
    cout.flush();

    // 所有"变化类"参数传 0 / nullptr，initOutputContext 内部会自动沿用源值：
    //   outWidth=0 / outHeight=0  → 沿用源分辨率
    //   targetFps=0.0             → 沿用源帧率
    //   targetSampleRate=0        → 沿用源采样率
    //   presetStr=nullptr         → 不设置 preset
    //   videoBitrate/audioBitrate → 直接传源码率（若源为 0 则由 initOutputContext 回退默认）
    // keepSourceChannelLayout=true → 沿用源声道布局（不强制立体声）
    // 编码线程数仍由成员变量 encodeThreads（config.dat 配置）控制，在此函数内无需额外设置
    return initOutputContext(
            videoID, audioID,
            srcVideoBitrate,
            nullptr,
            0.0,
            srcAudioBitrate,
            0, 0,
            0,
            true,   // allowHardware=true：若源为 H264/H265 可自动启用 mediacodec 硬编
            true    // keepSourceChannelLayout=true：沿用源声道布局
    );
}

// ====================================================
// 无参编码循环：编码完整文件（从 0 到结束），内部委托给核心实现
// ====================================================
int FFmpeg::encodeToFile() {
    return encodeToFileCore(0, (uint64_t)-1, false, 0, 1.0f);
}

// ============================
// 带时间范围的编码循环：必须显式传入起止时间，无默认值
// ============================
int FFmpeg::encodeToFile(uint64_t startTimeUs, uint64_t endTimeUs) {
    return encodeToFileCore(startTimeUs, endTimeUs, false, 0, 1.0f);
}

// ==================================================
// 带亮度/伽马调整的编码循环：使用滤镜链(eq)做亮度/伽马调整
// ==================================================
int FFmpeg::encodeToFile(int8_t brightness, float gamma) {
    cout << String("亮度调整: ", "Brightness: ")
         << "brightness=" << (int)brightness << " gamma=" << gamma << endl;
    return encodeToFileCore(0, (uint64_t)-1, true, brightness, gamma);
}

// ============================
// 核心编码循环：所有 encodeToFile 重载的唯一实现
// 使用 FFmpeg 滤镜链处理视频帧：可选 eq(亮度/伽马) → format → scale
// ============================
int FFmpeg::encodeToFileCore(uint64_t startTimeUs, uint64_t endTimeUs,
                              bool adjustBrightness, int8_t brightness, float gamma) {
    LOGI("遍码线程: %hhu", encodeThreads);
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
    int outChannels = 0;
    uint64_t outChannelMask = 0;
    if (audioStreamOk) {
        encFrameSize = aenc_.raw()->frame_size;
        varFrameSize = !!(aenc_.raw()->codec->capabilities & AV_CODEC_CAP_VARIABLE_FRAME_SIZE);
        outChannels = aenc_.raw()->ch_layout.nb_channels;
        outChannelMask = aenc_.raw()->ch_layout.u.mask;
        if (outChannelMask == 0) {
            AVChannelLayout defLayout = {};
            av_channel_layout_default(&defLayout, outChannels);
            outChannelMask = defLayout.u.mask;
        }
        if (!varFrameSize && encFrameSize > 0) {
            afifo = av_audio_fifo_alloc(outSampleFmt, outChannels, encFrameSize * 4);
        }
    }

    // 视频滤镜图（第一帧到达时初始化，因为需要源格式/尺寸/time_base）
    AVFilterGraph *filterGraph = nullptr;
    AVFilterContext *buffersrcCtx = nullptr;
    AVFilterContext *buffersinkCtx = nullptr;
    bool filterInited = false;

    int64_t aEncNextPts = 0;
    int64_t vEncNextPts = 0;
    int writtenVPkts = 0;
    int writtenAPkts = 0;
    AVRational vInTimeBase = {0, 0};
    AVRational aInTimeBase = {0, 0};
    if (videoStreamOk) vInTimeBase = fmtCtx_.raw()->streams[videoStreamIndex_]->time_base;
    if (audioStreamOk) aInTimeBase = fmtCtx_.raw()->streams[audioStreamIndex_]->time_base;

    // 帧率转换：目标帧率低于源帧率时丢帧，保持音视频同步
    double srcFps = 25.0;
    double targetFps = srcFps;
    double frameAccum = 0.0;
    if (videoStreamOk) {
        AVRational sfr = fmtCtx_.raw()->streams[videoStreamIndex_]->avg_frame_rate;
        if (sfr.num > 0 && sfr.den > 0) srcFps = av_q2d(sfr);
        if (venc_.raw()->framerate.num > 0 && venc_.raw()->framerate.den > 0)
            targetFps = av_q2d(venc_.raw()->framerate);
        if (targetFps > srcFps) targetFps = srcFps;
    }

    // 时间进度：总时长（微秒），未知则回退到包数进度
    // startTimeUs / endTimeUs 均为微秒单位（AV_TIME_BASE）
    int64_t totalDurationUs = fmtCtx_.raw()->duration;
    int64_t currentTimeUs = 0;
    if (endTimeUs != static_cast<uint64_t>(-1)) {
        if (endTimeUs > startTimeUs) totalDurationUs = (int64_t)(endTimeUs - startTimeUs);
        else {
            cout << String("设置时间失败...", "Failed to set duration...") << endl;
            return -EINVAL;
        }
    }
    const bool durationKnown = (totalDurationUs > 0 && totalDurationUs != AV_NOPTS_VALUE);

    cout << String("编码线程: ", "Encode threads: ") << static_cast<int>(encodeThreads) << endl;
    cout << String("正在编码...", "Encoding...") << endl;

    // 写包 lambda
    int64_t audioPktPts = 0;  // 手动维护音频包 pts（opus 编码器会忽略输入 pts）
    auto writePkt = [&](av::Packet &pkt, const av::Stream &st, const AVRational &encTb,
                         int64_t frameNbSamples) -> bool {
        if (!st.isValid() || !pkt) return false;
        AVPacket *rawPkt = pkt.raw();
        rawPkt->stream_index = st.index();
        bool isAudioPkt = (st.index() == outAStream_.index());
        // 补 duration：音频用样本数，视频每帧占 1 个编码器 time_base 单位
        if (rawPkt->duration <= 0) {
            if (isAudioPkt) {
                if (frameNbSamples > 0) {
                    rawPkt->duration = frameNbSamples;
                } else if (aenc_.isOpened() && aenc_.raw()->frame_size > 0) {
                    rawPkt->duration = aenc_.raw()->frame_size;
                }
            } else {
                rawPkt->duration = 1;
            }
        }
        // 关键修复：FFmpeg 内置原生 opus 编码器会忽略输入 frame->pts，
        // 自己维护一个从 0 开始的内部 pts 计数器，导致输出包的 pts 完全错误。
        // 这里对所有音频包手动维护 pts 序列，不依赖编码器输出的 pts。
        if (isAudioPkt) {
            rawPkt->pts = audioPktPts;
            rawPkt->dts = audioPktPts;
            audioPktPts += rawPkt->duration > 0 ? rawPkt->duration : 1;
        }
        std::error_code writeEc;
        outFmtCtx_.writePacket(pkt, writeEc);
        if (writeEc) {
            LOGD("写包失败: %s", writeEc.message().c_str());
            return false;
        }
        return true;
    };

    // 视频帧编码 lambda：滤镜链(eq→format→scale) → 编码
    auto feedVideoFrame = [&](av::VideoFrame &decFrame) {
        if (!venc_.isOpened() || !decFrame) return;
        // 帧率转换：目标帧率低于源帧率时按比例丢帧
        if (targetFps < srcFps - 0.01) {
            frameAccum += targetFps;
            if (frameAccum < srcFps) return;  // 丢帧
            frameAccum -= srcFps;
        }
        // 更新时间进度（取max，B帧乱序时进度不回退）
        if (durationKnown && decFrame.raw()->pts != AV_NOPTS_VALUE && vInTimeBase.num > 0) {
            int64_t frameUs = av_rescale_q(decFrame.raw()->pts, vInTimeBase, AV_TIME_BASE_Q);
            if (frameUs > currentTimeUs) currentTimeUs = frameUs;
        }
        // 时间范围过滤：起始时间之前 / 截止时间之后的帧不编码
        if (currentTimeUs < (int64_t)startTimeUs) return;
        if (endTimeUs != static_cast<uint64_t>(-1) && currentTimeUs > (int64_t)endTimeUs) return;

        // 第一帧时初始化滤镜图
        if (!filterInited && videoStreamOk) {
            filterGraph = avfilter_graph_alloc();
            if (filterGraph) {
                char args[512];
                AVRational tb = vInTimeBase.num > 0 ? vInTimeBase : (AVRational){1, 25};
                AVRational fr = fmtCtx_.raw()->streams[videoStreamIndex_]->avg_frame_rate;
                if (fr.den <= 0 || fr.num <= 0) fr = {25, 1};
                snprintf(args, sizeof(args),
                         "video_size=%dx%d:pix_fmt=%d:time_base=%d/%d:pixel_aspect=%d/%d:frame_rate=%d/%d",
                         decFrame.width(), decFrame.height(), decFrame.raw()->format,
                         tb.num, tb.den, 1, 1, fr.num, fr.den);

                const AVFilter *buffersrc  = avfilter_get_by_name("buffer");
                const AVFilter *buffersink = avfilter_get_by_name("buffersink");
                if (buffersrc && buffersink &&
                    avfilter_graph_create_filter(&buffersrcCtx, buffersrc, "in", args, nullptr, filterGraph) >= 0 &&
                    avfilter_graph_create_filter(&buffersinkCtx, buffersink, "out", nullptr, nullptr, filterGraph) >= 0) {

                    // 滤镜链：可选 eq(亮度/伽马) → format(编码器格式) → scale(输出尺寸)
                    // eq brightness 范围 -1.0~1.0，当前 -100~100 除以 100
                    char filterDesc[256];
                    const char *encFmtName = av_get_pix_fmt_name(venc_.raw()->pix_fmt);
                    if (adjustBrightness) {
                        float eqBright = brightness / 128.0f;
                        snprintf(filterDesc, sizeof(filterDesc),
                                 "eq=brightness=%.4f:gamma=%.4f,format=%s,scale=%d:%d",
                                 eqBright, gamma, encFmtName ? encFmtName : "yuv420p",
                                 outWidth, outHeight);
                    } else {
                        snprintf(filterDesc, sizeof(filterDesc),
                                 "format=%s,scale=%d:%d",
                                 encFmtName ? encFmtName : "yuv420p",
                                 outWidth, outHeight);
                    }

                    AVFilterInOut *outputs = avfilter_inout_alloc();
                    AVFilterInOut *inputs  = avfilter_inout_alloc();
                    outputs->name       = av_strdup("in");
                    outputs->filter_ctx = buffersrcCtx;
                    outputs->pad_idx    = 0;
                    outputs->next       = nullptr;
                    inputs->name        = av_strdup("out");
                    inputs->filter_ctx  = buffersinkCtx;
                    inputs->pad_idx     = 0;
                    inputs->next        = nullptr;

                    if (avfilter_graph_parse_ptr(filterGraph, filterDesc, &inputs, &outputs, nullptr) >= 0 &&
                        avfilter_graph_config(filterGraph, nullptr) >= 0) {
                        filterInited = true;
                        LOGD("滤镜初始化成功: %s", filterDesc);
                    } else {
                        LOGD("滤镜图解析/配置失败，回退直接编码");
                        avfilter_inout_free(&inputs);
                        avfilter_inout_free(&outputs);
                    }
                    avfilter_inout_free(&inputs);
                    avfilter_inout_free(&outputs);
                }
            }
            if (!filterInited) {
                LOGD("滤镜初始化失败，回退到直接编码（不做格式转换/缩放/亮度）");
            }
        }

        // 通过滤镜处理帧
        if (filterInited) {
            if (av_buffersrc_add_frame_flags(buffersrcCtx, decFrame.raw(), AV_BUFFERSRC_FLAG_KEEP_REF) < 0)
                return;
            while (true) {
                av::VideoFrame filtFrame;
                int ret = av_buffersink_get_frame(buffersinkCtx, filtFrame.raw());
                if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) break;
                if (ret < 0) return;

                filtFrame.raw()->pts = vEncNextPts;
                vEncNextPts++;
                std::error_code encEc;
                av::Packet encPkt = venc_.encode(filtFrame, encEc);
                if (encEc) {
                    LOGD("视频编码错误: %s", encEc.message().c_str());
                    return;
                }
                if (writePkt(encPkt, outVStream_, venc_.raw()->time_base, 0)) {
                    ++writtenVPkts;
                }
            }
        } else {
            // 回退：滤镜不可用时直接编码（要求源格式与编码器格式一致）
            decFrame.raw()->pts = vEncNextPts;
            vEncNextPts++;
            std::error_code encEc;
            av::Packet encPkt = venc_.encode(decFrame, encEc);
            if (encEc) {
                LOGD("视频编码错误: %s", encEc.message().c_str());
                return;
            }
            if (writePkt(encPkt, outVStream_, venc_.raw()->time_base, 0)) {
                ++writtenVPkts;
            }
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
        // 时间范围过滤：起始时间之前 / 截止时间之后的样本不编码
        if (currentTimeUs < (int64_t)startTimeUs) return;
        if (endTimeUs != static_cast<uint64_t>(-1) && currentTimeUs > (int64_t)endTimeUs) return;
        int dstNbSamples = swr_get_out_samples(swrCtx_, decSamples.samplesCount());
        if (dstNbSamples <= 0) return;
        av::AudioSamples encSamples(outSampleFmt, dstNbSamples,
                                    outChannelMask, aenc_.sampleRate());
        int swrRet = swr_convert(swrCtx_,
                                 encSamples.raw()->data, dstNbSamples,
                                 (const uint8_t **) decSamples.raw()->data,
                                 decSamples.samplesCount());
        if (swrRet < 0) return;
        if (afifo) {
            av_audio_fifo_write(afifo, (void **) encSamples.raw()->data, swrRet);
            while (av_audio_fifo_size(afifo) >= encFrameSize) {
                av::AudioSamples encFrame(outSampleFmt, encFrameSize,
                                          outChannelMask, aenc_.sampleRate());
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

    // ========== 起始时间 seek：跳到 startTimeUs 前的最近关键帧，避免从 0 秒逐包解码 ==========
    if (startTimeUs > 0) {
        // 优先用视频流 seek（有关键帧），无视频则用音频流
        int seekStreamIndex = hasVideo() ? videoStreamIndex_ :
                              (hasAudio() ? audioStreamIndex_ : -1);
        if (seekStreamIndex >= 0) {
            AVRational seekTb = fmtCtx_.raw()->streams[seekStreamIndex]->time_base;
            // 微秒 → 目标流 time_base 单位
            int64_t seekTs = av_rescale_q((int64_t)startTimeUs, AV_TIME_BASE_Q, seekTb);
            // AVSEEK_FLAG_BACKWARD：跳到不超过目标时间的最近关键帧
            int seekRet = av_seek_frame(fmtCtx_.raw(), seekStreamIndex, seekTs, AVSEEK_FLAG_BACKWARD);
            if (seekRet >= 0) {
                // seek 后必须 flush 解码器缓冲，否则会输出旧位置的帧
                if (videoStreamOk) avcodec_flush_buffers(vdec_.raw());
                if (audioStreamOk) avcodec_flush_buffers(adec_.raw());
                LOGD("seek 成功: stream=%d targetTs=%lld startTimeUs=%llu",
                     seekStreamIndex, (long long)seekTs, (unsigned long long)startTimeUs);
            } else {
                LOGD("seek 失败，回退到从开头解码: ret=%d", seekRet);
            }
        }
    }

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
                int64_t elapsedUs = (currentTimeUs > (int64_t)startTimeUs)
                                        ? (currentTimeUs - (int64_t)startTimeUs) : 0;
                int percent = (int)(elapsedUs * 100 / totalDurationUs);
                if (percent > 100) percent = 100;
                cout << String("进度: ", "Progress: ") << percent << "%  ("
                     << elapsedUs / 1000000 << "s / " << totalDurationUs / 1000000 << "s)"
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
        // 截止判断：feed 内部已过滤当前帧，这里用于及时退出主循环（避免解码剩余全部文件）
        if (endTimeUs != static_cast<uint64_t>(-1) && currentTimeUs > (int64_t)endTimeUs) break;
    }

    // ========== 冲刷解码器 ==========
    if (videoStreamOk) {
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
                                        outChannelMask, aenc_.sampleRate());
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
                                          outChannelMask, aenc_.sampleRate());
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
    // 兜底修正音视频流 duration（vEncNextPts/aEncNextPts 基于各自编码器 time_base）
    if (outVStream_.isValid() && venc_.isOpened()) {
        outVStream_.raw()->duration = av_rescale_q(vEncNextPts, venc_.raw()->time_base,
                                                     outVStream_.raw()->time_base);
    }
    if (outAStream_.isValid() && aenc_.isOpened()) {
        outAStream_.raw()->duration = av_rescale_q(aEncNextPts, {1, aenc_.sampleRate()},
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
    if (filterGraph) avfilter_graph_free(&filterGraph);
    return 0;
}

// ============================
// 便捷重载：用指定名称的编码器初始化输出并编码写出文件
// 内部等价于：name→codec_id → openOutPutWithEncoder() → encodeToFile()
// ============================
[[maybe_unused]]
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

extern "C" JNIEXPORT jlong JNICALL
Java_com_kgmdecoder_app_Selecting_durationUs(JNIEnv *env, jobject thiz) {
    return g_fileDurationUs.load();
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
