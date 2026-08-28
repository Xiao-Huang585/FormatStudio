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
}

FFmpeg *ffmpeg = nullptr;

// ============================
// 构造/析构
// ============================
FFmpeg::FFmpeg(androidOutStream &os, androidInStream &is)
        : cout(os), cin(is), inited(true), outPath_("/sdcard/Download/default.mp4") {}

FFmpeg::~FFmpeg() {
    close();
}

void FFmpeg::init(androidOutStream &os, androidInStream &is) {

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
// 用指定编码器打开输出文件
// ============================

int FFmpeg::openOutPutWithEncoder(AVCodecID videoID, AVCodecID audioID) {
    outFmtCtx_.openOutput(outPath_);
    if (!fmtCtx_.isOpened()) {
        cout << String("未打开输入文件...", "Didn't open input file...") << endl;
        LOGD("未打开输入文件...");
        return -EINVAL;
    }

    if (!hasVideo() && !hasAudio()) {
        cout << String("未找到音视频流...", "Can not find video/audio stream...") << endl;
        LOGD("未找到音视频流...");
        return -EINVAL;
    }

    std::error_code ec;
    outFmtCtx_.openOutput(outPath_, ec);
    if (ec) {
        cout << String("打开输出文件失败...", "Fail to open output file...") << endl;
        LOGD("打开输出文件失败...");
        return ec.value();
    }

    // 视频初始化
    if (hasVideo() && videoID != AV_CODEC_ID_NONE) {
        av::Codec vCodec = av::findEncodingCodec(videoID);
        if (vCodec.isNull()) {
            cout << String("找不到视频编码器...", "Can not find video encoder...") << endl;
            LOGD("找不到视频编码器...");
            return -EINVAL;
        }

        venc_ = av::VideoEncoderContext(vCodec);

        int outWidth = vdec_.width();
        int outHeight = vdec_.height();
        venc_.setWidth(outWidth);
        venc_.setHeight(outHeight);
        venc_.setPixelFormat(AV_PIX_FMT_YUV420P);
        AVRational inFrameRate = fmtCtx_.raw()->streams[videoStreamIndex_]->avg_frame_rate;
        if (inFrameRate.den <= 0 || inFrameRate.num <= 0) {
            inFrameRate = {25, 1};
        }
        venc_.setTimeBase(av::Rational(inFrameRate.num, inFrameRate.den));
        venc_.setGopSize(50);
        if (videoID == AV_CODEC_ID_H264 || videoID == AV_CODEC_ID_H265) {
            venc_.setOption("preset", "medium");
            venc_.setOption("crf", "27");
        }
        venc_.open(ec);
        if (ec) {
            cout << String("打开视频编码器失败:", "Fail to open video encoder: ") << ec.message() << endl;
            LOGD("打开视频编码器失败: %s", ec.message().c_str());
            return ec.value();
        }
        outVStream_ = outFmtCtx_.addStream(venc_, ec);
        if (ec) {
            cout << String("创建输出视频流失败: ", "Fail to create output video stream: ") << ec.message() << endl;
            LOGD("创建输出视频流失败: %s", ec.message().c_str());
            return ec.value();
        }
        swsCtx_ = sws_getContext(
                vdec_.width(), vdec_.height(),
                vdec_.pixelFormat(),
                outWidth, outHeight,
                AV_PIX_FMT_YUV420P,
                SWS_BILINEAR, nullptr, nullptr, nullptr
        );
        if (!swsCtx_) {
            cout << String("初始化像素格式失败...", "Fail to init pixel format...") << endl;
            LOGD("初始化像素格式失败...");
            return -ENOMEM;
        }

        cout << String("视频编码器: ", "Video encoder: ") << avcodec_get_name(videoID) << '\n' << outWidth << 'x' <<
             outHeight << endl;
    }

    // 音频初始化
    if (hasAudio() && audioID != AV_CODEC_ID_NONE) {
        av::Codec aCodec = av::findEncodingCodec(audioID);
        if (aCodec.isNull()) {
            cout << String("找不到音频编码器, 已取消音频...",
                           "Can not find audio stream, audio stream is canceled now.") << endl;
            LOGD("找不到音频编码器, 已取消音频...");
        } else {
            aenc_ = av::AudioEncoderContext(aCodec);

            aenc_.setSampleRate(adec_.sampleRate());
            aenc_.setBitRate(adec_.bitRate());
            aenc_.setTimeBase(av::Rational(1, adec_.sampleRate()));

            AVSampleFormat outSampleFmt;
            if (audioID == AV_CODEC_ID_AAC) {
                outSampleFmt = AV_SAMPLE_FMT_FLTP;
            } else if (audioID == AV_CODEC_ID_MP3) {
                outSampleFmt = AV_SAMPLE_FMT_FLTP;
            } else {
                outSampleFmt = AV_SAMPLE_FMT_FLTP;
            }
            aenc_.setSampleFormat(outSampleFmt);

            AVChannelLayout stereoLayout = AV_CHANNEL_LAYOUT_STEREO;

            av_channel_layout_copy(&aenc_.raw()->ch_layout, &stereoLayout);

            aenc_.open(ec);
            if (ec) {
                cout << String("打开音频编码器失败: ", "Fail to open audio encoder: ") << ec.message() << endl;
                LOGD("打开音频编码器失败: %s", ec.message().c_str());
                aenc_ = av::AudioEncoderContext();
            } else {
                outAStream_ = outFmtCtx_.addStream(aenc_, ec);
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
                    cout << String("音频编码器: ", "Audio encoder: ") << avcodec_get_name(audioID) << '\n' <<
                         adec_.bitRate() / 1000 << "kbps" << '\n' << adec_.sampleRate() << "Hz" << endl;
                }
            }
        }
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
// 压缩/转码
// ============================
int FFmpeg::compressMedia(const char* outputPath,
                          int targetBitRateKbps,
                          int targetWidth,
                          int targetHeight,
                          int crf,
                          const char* preset) {
    if (!fmtCtx_.isOpened() || !outputPath || !outputPath[0]) {
        return -EINVAL;
    }
    if (!hasVideo() && !hasAudio()) {
        cout << "当前媒体没有音视频流，无法压缩" << endl;
        return -EINVAL;
    }

    LOGD("开始压缩");
    cout << "===== 开始压缩 =====" << endl;
    cout << "输出: " << outputPath << endl;

    std::error_code ec;

    // 1. 创建输出格式上下文
    av::FormatContext outCtx;
    outCtx.openOutput(outputPath, ec);
    if (ec) {
        cout << "创建输出上下文失败: " << ec.message() << endl;
        return ec.value();
    }

    // 关键：mp4/m4a/flac 等容器带 AVFMT_GLOBALHEADER 标志，
    // 编码器必须在 avcodec_open2 之前设置 AV_CODEC_FLAG_GLOBAL_HEADER，
    // 否则不会生成 extradata(h264 的 SPS/PPS、aac 的 ASC)，
    // 写出的 mp4 缺少 avcC/esds 盒子 → 播放器无法初始化解码器。
    // avcpp 的 addStream() 不会自动设置，必须手动加！
    const bool needGlobalHeader = !!(outCtx.raw()->oformat->flags & AVFMT_GLOBALHEADER);

    // 2. 视频编码器初始化
    av::VideoEncoderContext vEnc;
    av::Stream outVStream;
    SwsContext* swsCtx = nullptr;
    int outWidth = 0, outHeight = 0;

    if (hasVideo()) {
        av::Codec vCodec = av::findEncodingCodec(AV_CODEC_ID_H264);
        if (vCodec.isNull()) {
            cout << "找不到 H.264 编码器" << endl;
            return -EINVAL;
        }

        vEnc = av::VideoEncoderContext(vCodec);

        outWidth  = (targetWidth > 0)  ? targetWidth  : vdec_.width();
        outHeight = (targetHeight > 0) ? targetHeight : vdec_.height();
        outWidth  &= ~1;
        outHeight &= ~1;

        vEnc.setWidth(outWidth);
        vEnc.setHeight(outHeight);
        vEnc.setPixelFormat(AV_PIX_FMT_YUV420P);

        AVRational inFrameRate = fmtCtx_.raw()->streams[videoStreamIndex_]->avg_frame_rate;
        if (inFrameRate.den <= 0 || inFrameRate.num <= 0) {
            inFrameRate = {25, 1};
        }
        vEnc.setTimeBase(av::Rational(inFrameRate.den, inFrameRate.num));
        vEnc.setBitRate(static_cast<int64_t>(targetBitRateKbps) * 1000);
        vEnc.setGopSize(50);

        vEnc.setOption("preset", preset);
        {
            char crfStr[16];
            snprintf(crfStr, sizeof(crfStr), "%d", crf);
            vEnc.setOption("crf", crfStr);
        }

        if (needGlobalHeader) {
            vEnc.raw()->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
        }

        vEnc.open(ec);
        if (ec) {
            cout << "打开视频编码器失败: " << ec.message() << endl;
            return ec.value();
        }

        outVStream = outCtx.addStream(vEnc, ec);
        if (ec) {
            cout << "创建输出视频流失败: " << ec.message() << endl;
            return ec.value();
        }

        swsCtx = sws_getContext(
                vdec_.width(), vdec_.height(), vdec_.pixelFormat(),
                outWidth, outHeight, AV_PIX_FMT_YUV420P,
                SWS_BILINEAR, nullptr, nullptr, nullptr);

        cout << "视频编码: " << outWidth << "x" << outHeight
             << " crf=" << crf << " preset=" << preset << endl;
    }

    // 3. 音频编码器初始化
    av::AudioEncoderContext aEnc;
    av::Stream outAStream;
    SwrContext* swrCtx = nullptr;
    // 音频 FIFO：aac 要求每帧恰好 1024 个样本，mp3 解码每包 1152 个，必须切帧
    AVAudioFifo *afifo = nullptr;
    int encFrameSize = 0;
    bool varFrameSize = false;

    if (hasAudio()) {
        av::Codec aCodec = av::findEncodingCodec(AV_CODEC_ID_AAC);
        if (!aCodec.isNull()) {
            aEnc = av::AudioEncoderContext(aCodec);

            aEnc.setSampleRate(adec_.sampleRate());
            aEnc.setSampleFormat(AV_SAMPLE_FMT_FLTP);
            aEnc.setBitRate(128 * 1000);
            aEnc.setTimeBase(av::Rational(1, adec_.sampleRate()));

            AVChannelLayout stereoLayout = AV_CHANNEL_LAYOUT_STEREO;
            av_channel_layout_copy(&aEnc.raw()->ch_layout, &stereoLayout);

            if (needGlobalHeader) {
                aEnc.raw()->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
            }

            aEnc.open(ec);
            if (ec) {
                cout << "打开音频编码器失败: " << ec.message() << endl;
                aEnc = av::AudioEncoderContext();
            } else {
                // 初始化重采样上下文
                AVChannelLayout outLayout = AV_CHANNEL_LAYOUT_STEREO;
                AVChannelLayout inLayout = {};
                av_channel_layout_copy(&inLayout, &adec_.raw()->ch_layout);

                int swrRet = swr_alloc_set_opts2(&swrCtx,
                                                 &outLayout, AV_SAMPLE_FMT_FLTP, adec_.sampleRate(),
                                                 &inLayout, adec_.sampleFormat(), adec_.sampleRate(),
                                                 0, nullptr);

                av_channel_layout_uninit(&inLayout);

                bool swrOk = false;
                if (swrCtx && swrRet >= 0) {
                    swrRet = swr_init(swrCtx);
                    if (swrRet < 0) {
                        cout << "初始化重采样失败" << endl;
                        swr_free(&swrCtx);
                        swrCtx = nullptr;
                    } else {
                        swrOk = true;
                    }
                }

                if (!swrOk) {
                    aEnc = av::AudioEncoderContext();
                } else {
                    // 编码器打开后才能读到 frame_size（aac=1024）
                    encFrameSize = aEnc.raw()->frame_size;
                    varFrameSize = !!(aCodec.raw()->capabilities & AV_CODEC_CAP_VARIABLE_FRAME_SIZE);
                    if (!varFrameSize && encFrameSize > 0) {
                        afifo = av_audio_fifo_alloc(AV_SAMPLE_FMT_FLTP, 2 /*stereo*/, encFrameSize * 4);
                    }

                    outAStream = outCtx.addStream(aEnc, ec);
                    if (ec) {
                        cout << "创建输出音频流失败: " << ec.message() << endl;
                        aEnc = av::AudioEncoderContext();
                        if (afifo) {
                            av_audio_fifo_free(afifo);
                            afifo = nullptr;
                        }
                    } else {
                        cout << "音频编码: AAC 128kbps" << endl;
                    }
                }
            }
        } else {
            cout << "找不到 AAC 编码器" << endl;
        }
    }

    // 4. 写文件头
    outCtx.writeHeader(ec);
    if (ec) {
        cout << "写文件头失败: " << ec.message() << endl;
        if (swsCtx) sws_freeContext(swsCtx);
        if (swrCtx) swr_free(&swrCtx);
        return ec.value();
    }

    // 5. 创建 YUV 帧（avcpp VideoFrame 构造函数自动分配缓冲区）
    av::VideoFrame yuvFrame;
    int64_t aEncNextPts = 0;   // 音频 pts（按样本数累计）
    int writtenVPkts = 0;
    int writtenAPkts = 0;

    if (hasVideo() && vEnc.isOpened()) {
        yuvFrame = av::VideoFrame(AV_PIX_FMT_YUV420P, outWidth, outHeight);
    }

    // 获取时间基
    AVRational vInTimeBase = {0, 0};
    if (hasVideo()) {
        vInTimeBase = fmtCtx_.raw()->streams[videoStreamIndex_]->time_base;
    }

    cout << "正在压缩..." << endl;

    // ------------------------------------------------------------
    // avcpp 语义陷阱：无参 encode() 会发送 null frame（flush 信号），
    // 编码中途调用会让编码器进入 drain 状态，之后的帧全部被拒绝。
    // 每帧只调用一次 encode(frame)，至多取回一个包。
    // ------------------------------------------------------------

    // 写出一个已编码包
    auto writePkt = [&](av::Packet &pkt, const av::Stream &st, const AVRational &encTb) -> bool {
        if (!st.isValid() || !pkt) return false;
        pkt.raw()->stream_index = st.index();
        av_packet_rescale_ts(pkt.raw(), encTb, st.raw()->time_base);
        std::error_code writeEc;
        outCtx.writePacket(pkt, writeEc);
        if (writeEc) {
            LOGD("写包失败: %s", writeEc.message().c_str());
            return false;
        }
        return true;
    };

    // 处理一帧解码后的视频
    auto feedVideoFrame = [&](av::VideoFrame &decFrame) {
        if (!swsCtx || !vEnc.isOpened() || !decFrame) return;

        // 像素格式转换（通过 raw() 访问底层 AVFrame）
        sws_scale(swsCtx,
                  decFrame.raw()->data, decFrame.raw()->linesize,
                  0, decFrame.height(),
                  yuvFrame.raw()->data, yuvFrame.raw()->linesize);

        yuvFrame.raw()->pts = av_rescale_q(decFrame.raw()->pts,
                                           vInTimeBase, vEnc.raw()->time_base);

        std::error_code encEc;
        av::Packet encPkt = vEnc.encode(yuvFrame, encEc);
        if (encEc) {
            LOGD("视频编码错误: %s", encEc.message().c_str());
            return;
        }
        if (writePkt(encPkt, outVStream, vEnc.raw()->time_base)) {
            ++writtenVPkts;
        }
    };

    // 处理一段解码后的音频：重采样 → FIFO 切帧 → 编码
    auto feedAudioSamples = [&](av::AudioSamples &decSamples) {
        if (!swrCtx || !aEnc.isOpened() || !decSamples) return;

        int dstNbSamples = swr_get_out_samples(swrCtx, decSamples.samplesCount());
        if (dstNbSamples <= 0) return;

        av::AudioSamples encSamples(AV_SAMPLE_FMT_FLTP, dstNbSamples,
                                    AV_CH_LAYOUT_STEREO, adec_.sampleRate());

        int swrRet = swr_convert(swrCtx,
                                 encSamples.raw()->data, dstNbSamples,
                                 (const uint8_t**)decSamples.raw()->data,
                                 decSamples.samplesCount());
        if (swrRet < 0) return;

        if (afifo) {
            av_audio_fifo_write(afifo, (void**)encSamples.raw()->data, swrRet);
            while (av_audio_fifo_size(afifo) >= encFrameSize) {
                av::AudioSamples encFrame(AV_SAMPLE_FMT_FLTP, encFrameSize,
                                          AV_CH_LAYOUT_STEREO, adec_.sampleRate());
                av_audio_fifo_read(afifo, (void**)encFrame.raw()->data, encFrameSize);

                encFrame.raw()->pts = aEncNextPts;
                aEncNextPts += encFrameSize;

                std::error_code encEc;
                av::Packet encPkt = aEnc.encode(encFrame, encEc);
                if (encEc) {
                    LOGD("音频编码错误: %s", encEc.message().c_str());
                    return;
                }
                if (writePkt(encPkt, outAStream, aEnc.raw()->time_base)) {
                    ++writtenAPkts;
                }
            }
        } else {
            encSamples.raw()->nb_samples = swrRet;
            encSamples.raw()->pts = aEncNextPts;
            aEncNextPts += swrRet;

            std::error_code encEc;
            av::Packet encPkt = aEnc.encode(encSamples, encEc);
            if (encEc) {
                LOGD("音频编码错误: %s", encEc.message().c_str());
                return;
            }
            if (writePkt(encPkt, outAStream, aEnc.raw()->time_base)) {
                ++writtenAPkts;
            }
        }
    };

    // 6. 主转码循环
    while (true) {
        std::error_code readEc;
        av::Packet pkt = fmtCtx_.readPacket(readEc);
        if (readEc) break;   // 读取错误
        if (!pkt) break;     // 文件结束

        bool isVideo = (hasVideo() && pkt.streamIndex() == videoStreamIndex_);
        bool isAudio = (hasAudio() && aEnc.isOpened() && pkt.streamIndex() == audioStreamIndex_);

        if (!isVideo && !isAudio) {
            continue;
        }

        // ---- 视频处理 ----
        if (isVideo && vEnc.isOpened()) {
            std::error_code decEc;
            av::VideoFrame decFrame = vdec_.decode(pkt, decEc);
            if (decEc) {
                LOGD("视频解码错误: %s", decEc.message().c_str());
                continue;
            }
            if (decFrame) {
                feedVideoFrame(decFrame);
            }
        }

        // ---- 音频处理 ----
        if (isAudio && swrCtx) {
            std::error_code decEc;
            av::AudioSamples decSamples = adec_.decode(pkt, decEc);
            if (decEc) {
                LOGD("音频解码错误: %s", decEc.message().c_str());
                continue;
            }
            if (decSamples) {
                feedAudioSamples(decSamples);
            }
        }
    }

    // 7. 冲洗解码器（取出解码器内部缓存的最后几帧）
    if (hasVideo() && vEnc.isOpened() && swsCtx) {
        avcodec_send_packet(vdec_.raw(), nullptr);
        while (true) {
            std::error_code decEc;
            av::VideoFrame decFrame = vdec_.decode(av::Packet(), decEc);
            if (decEc || !decFrame) break;
            feedVideoFrame(decFrame);
        }
    }
    if (hasAudio() && aEnc.isOpened() && swrCtx) {
        avcodec_send_packet(adec_.raw(), nullptr);
        while (true) {
            std::error_code decEc;
            av::AudioSamples decSamples = adec_.decode(av::Packet(), decEc);
            if (decEc || !decSamples) break;
            feedAudioSamples(decSamples);
        }
    }

    // 8. 冲洗重采样器 + FIFO 中剩余样本
    if (aEnc.isOpened() && swrCtx) {
        while (true) {
            int avail = swr_get_out_samples(swrCtx, 0);
            if (avail <= 0) break;

            av::AudioSamples encSamples(AV_SAMPLE_FMT_FLTP, avail,
                                        AV_CH_LAYOUT_STEREO, adec_.sampleRate());
            int swrRet = swr_convert(swrCtx,
                                     encSamples.raw()->data, avail,
                                     nullptr, 0);
            if (swrRet <= 0) break;

            if (afifo) {
                av_audio_fifo_write(afifo, (void**)encSamples.raw()->data, swrRet);
            } else {
                encSamples.raw()->nb_samples = swrRet;
                encSamples.raw()->pts = aEncNextPts;
                aEncNextPts += swrRet;

                std::error_code encEc;
                av::Packet encPkt = aEnc.encode(encSamples, encEc);
                if (writePkt(encPkt, outAStream, aEnc.raw()->time_base)) {
                    ++writtenAPkts;
                }
            }
        }

        if (afifo) {
            while (av_audio_fifo_size(afifo) > 0) {
                int fifoSize = av_audio_fifo_size(afifo);
                int n = (fifoSize < encFrameSize) ? fifoSize : encFrameSize;

                av::AudioSamples encFrame(AV_SAMPLE_FMT_FLTP, n,
                                          AV_CH_LAYOUT_STEREO, adec_.sampleRate());
                av_audio_fifo_read(afifo, (void**)encFrame.raw()->data, n);

                encFrame.raw()->pts = aEncNextPts;
                aEncNextPts += n;

                std::error_code encEc;
                av::Packet encPkt = aEnc.encode(encFrame, encEc);
                if (writePkt(encPkt, outAStream, aEnc.raw()->time_base)) {
                    ++writtenAPkts;
                }
            }
        }
    }

    // 9. 冲洗编码器（此时才允许用无参 encode() 发送 null frame）
    if (vEnc.isOpened()) {
        while (true) {
            std::error_code encEc;
            av::Packet encPkt = vEnc.encode(encEc);
            if (!encPkt) break;
            if (writePkt(encPkt, outVStream, vEnc.raw()->time_base)) {
                ++writtenVPkts;
            }
        }
    }
    if (aEnc.isOpened()) {
        while (true) {
            std::error_code encEc;
            av::Packet encPkt = aEnc.encode(encEc);
            if (!encPkt) break;
            if (writePkt(encPkt, outAStream, aEnc.raw()->time_base)) {
                ++writtenAPkts;
            }
        }
    }

    // 10. 写文件尾
    outCtx.writeTrailer(ec);

    cout << "压缩完成！视频包: " << writtenVPkts << ", 音频包: " << writtenAPkts << endl;

    // 11. 释放资源（avcpp 对象通过 RAII 自动释放，仅清理 C API 资源）
    if (swsCtx) sws_freeContext(swsCtx);
    if (swrCtx) swr_free(&swrCtx);
    if (afifo) av_audio_fifo_free(afifo);

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
// 用指定名称的编码器重新编码输出
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
    std::error_code ec;

    const AVOutputFormat *ofmt = av_guess_format(nullptr, outputPath, nullptr);
    if (!ofmt) {
        cout << String("无法根据扩展名识别输出格式: ", "Can't recognize output format by extension: ")
             << outputPath << endl;
        LOGD("av_guess_format 失败: %s", outputPath);
        return -EINVAL;
    }
    if (wantVideo) {
        const AVCodec *probe = avcodec_find_encoder_by_name(videoEncoderName);
        if (!probe) {
            cout << String("找不到视频编码器: ", "Can not find video encoder: ")
                 << videoEncoderName << endl;
            LOGD("找不到视频编码器: %s", videoEncoderName);
            return -EINVAL;
        }
        if (avformat_query_codec(ofmt, probe->id, FF_COMPLIANCE_NORMAL) == 0) {
            cout << String("容器不支持视频编码器 ", "Container doesn't support video encoder ")
                 << videoEncoderName;
            cout << String("，已跳过视频流", ", video stream skipped") << endl;
            LOGD("容器不支持视频编码器 %s，跳过视频流", videoEncoderName);
            wantVideo = false;
        }
    }
    if (wantAudio) {
        const AVCodec *probe = avcodec_find_encoder_by_name(audioEncoderName);
        if (!probe) {
            cout << String("找不到音频编码器: ", "Can not find audio encoder: ")
                 << audioEncoderName << endl;
            LOGD("找不到音频编码器: %s", audioEncoderName);
            return -EINVAL;
        }
        if (avformat_query_codec(ofmt, probe->id, FF_COMPLIANCE_NORMAL) == 0) {
            cout << String("容器不支持音频编码器 ", "Container doesn't support audio encoder ")
                 << audioEncoderName;
            cout << String("，已跳过音频流", ", audio stream skipped") << endl;
            LOGD("容器不支持音频编码器 %s，跳过音频流", audioEncoderName);
            wantAudio = false;
        }
    }
    if (!wantVideo && !wantAudio) {
        cout << String("没有可编码的音视频流...", "No audio/video stream to encode...") << endl;
        return -EINVAL;
    }

    av::FormatContext outCtx;
    outCtx.openOutput(outputPath, ec);
    if (ec) {
        cout << String("创建输出上下文失败: ", "Fail to create output context: ") << ec.message() << endl;
        LOGD("创建输出上下文失败: %s", ec.message().c_str());
        return ec.value();
    }
    const bool needGlobalHeader = !!(outCtx.raw()->oformat->flags & AVFMT_GLOBALHEADER);
    LOGD("输出容器: %s, needGlobalHeader=%d", ofmt->name, (int)needGlobalHeader);

    av::VideoEncoderContext vEnc;
    av::Stream outVStream;
    SwsContext* swsCtx = nullptr;
    int outWidth = 0, outHeight = 0;
    bool videoStreamOk = false;
    if (wantVideo) {
        const AVCodec *vCodecRaw = avcodec_find_encoder_by_name(videoEncoderName);
        if (!vCodecRaw) {
            cout << String("找不到视频编码器: ", "Can not find video encoder: ")
                 << videoEncoderName << endl;
            return -EINVAL;
        }
        av::Codec vCodec(vCodecRaw);
        vEnc = av::VideoEncoderContext(vCodec);
        outWidth  = vdec_.width();
        outHeight = vdec_.height();
        outWidth  &= ~1;
        outHeight &= ~1;
        vEnc.setWidth(outWidth);
        vEnc.setHeight(outHeight);
        vEnc.setPixelFormat(AV_PIX_FMT_YUV420P);
        AVRational inFrameRate = fmtCtx_.raw()->streams[videoStreamIndex_]->avg_frame_rate;
        if (inFrameRate.den <= 0 || inFrameRate.num <= 0) {
            inFrameRate = {25, 1};
        }
        vEnc.setTimeBase(av::Rational(inFrameRate.den, inFrameRate.num));
        vEnc.setGopSize(50);
        if (strstr(videoEncoderName, "264") || strstr(videoEncoderName, "265")) {
            vEnc.setOption("preset", "medium");
            vEnc.setOption("crf", "23");
        } else {
            vEnc.setBitRate(2000000);
        }
        if (needGlobalHeader) {
            vEnc.raw()->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
        }
        vEnc.open(ec);
        if (ec) {
            cout << String("打开视频编码器失败: ", "Fail to open video encoder: ")
                 << ec.message() << endl;
            LOGD("打开视频编码器失败: %s", ec.message().c_str());
            return ec.value();
        }
        swsCtx = sws_getContext(
                vdec_.width(), vdec_.height(), vdec_.pixelFormat(),
                outWidth, outHeight, AV_PIX_FMT_YUV420P,
                SWS_BILINEAR, nullptr, nullptr, nullptr);
        if (!swsCtx) {
            cout << String("初始化像素格式转换失败...", "Fail to init sws...") << endl;
            LOGD("sws_getContext 失败");
            vEnc = av::VideoEncoderContext();
        } else {
            outVStream = outCtx.addStream(vEnc, ec);
            if (ec) {
                cout << String("创建输出视频流失败: ", "Fail to create output video stream: ")
                     << ec.message() << endl;
                LOGD("创建输出视频流失败: %s", ec.message().c_str());
                vEnc = av::VideoEncoderContext();
                sws_freeContext(swsCtx);
                swsCtx = nullptr;
            } else {
                videoStreamOk = true;
                cout << String("视频编码器: ", "Video encoder: ") << videoEncoderName
                     << " (" << outWidth << "x" << outHeight << ")" << endl;
            }
        }
    }

    av::AudioEncoderContext aEnc;
    av::Stream outAStream;
    SwrContext* swrCtx = nullptr;
    AVSampleFormat outSampleFmt = AV_SAMPLE_FMT_NONE;
    AVAudioFifo *afifo = nullptr;
    int encFrameSize = 0;
    bool varFrameSize = false;
    bool audioStreamOk = false;
    if (wantAudio) {
        const AVCodec *aCodecRaw = avcodec_find_encoder_by_name(audioEncoderName);
        if (!aCodecRaw) {
            cout << String("找不到音频编码器: ", "Can not find audio encoder: ")
                 << audioEncoderName << endl;
            if (swrCtx) swr_free(&swrCtx);
            return -EINVAL;
        }
        av::Codec aCodec(aCodecRaw);
        aEnc = av::AudioEncoderContext(aCodec);
        outSampleFmt = pickEncoderSampleFormat(aCodecRaw);
        aEnc.setSampleRate(adec_.sampleRate());
        aEnc.setSampleFormat(outSampleFmt);
        aEnc.setBitRate(128 * 1000);
        aEnc.setTimeBase(av::Rational(1, adec_.sampleRate()));
        AVChannelLayout stereoLayout = AV_CHANNEL_LAYOUT_STEREO;
        av_channel_layout_copy(&aEnc.raw()->ch_layout, &stereoLayout);
        if (needGlobalHeader) {
            aEnc.raw()->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
        }
        aEnc.open(ec);
        if (ec) {
            cout << String("打开音频编码器失败: ", "Fail to open audio encoder: ")
                 << ec.message() << endl;
            LOGD("打开音频编码器失败: %s", ec.message().c_str());
            aEnc = av::AudioEncoderContext();
        } else {
            AVChannelLayout outLayout = AV_CHANNEL_LAYOUT_STEREO;
            AVChannelLayout inLayout = {};
            av_channel_layout_copy(&inLayout, &adec_.raw()->ch_layout);
            int swrRet = swr_alloc_set_opts2(&swrCtx,
                                             &outLayout, outSampleFmt, adec_.sampleRate(),
                                             &inLayout, adec_.sampleFormat(), adec_.sampleRate(),
                                             0, nullptr);
            av_channel_layout_uninit(&inLayout);
            bool swrOk = false;
            if (swrCtx && swrRet >= 0) {
                swrRet = swr_init(swrCtx);
                if (swrRet < 0) {
                    cout << String("初始化重采样失败...", "Fail to init swr...") << endl;
                    swr_free(&swrCtx);
                    swrCtx = nullptr;
                } else {
                    swrOk = true;
                }
            }
            if (!swrOk) {
                aEnc = av::AudioEncoderContext();
            } else {
                encFrameSize = aEnc.raw()->frame_size;
                varFrameSize = !!(aCodecRaw->capabilities & AV_CODEC_CAP_VARIABLE_FRAME_SIZE);
                if (!varFrameSize && encFrameSize > 0) {
                    afifo = av_audio_fifo_alloc(outSampleFmt, 2 /*stereo*/, encFrameSize * 4);
                }
                outAStream = outCtx.addStream(aEnc, ec);
                // =========修复复制bit_rate=========
                if(outAStream.isValid()){
                    outAStream.raw()->codecpar->bit_rate = aEnc.raw()->bit_rate;
                }
                if (ec) {
                    cout << String("创建输出音频流失败: ", "Fail to create output audio stream: ")
                         << ec.message() << endl;
                    LOGD("创建输出音频流失败: %s", ec.message().c_str());
                    aEnc = av::AudioEncoderContext();
                    if (afifo) {
                        av_audio_fifo_free(afifo);
                        afifo = nullptr;
                    }
                } else {
                    audioStreamOk = true;
                    cout << String("音频编码器: ", "Audio encoder: ") << audioEncoderName << endl;
                }
            }
        }
    }

    if (!videoStreamOk && !audioStreamOk) {
        cout << String("音视频输出流均未建立，编码取消...", "No output stream created, encoding canceled...") << endl;
        LOGD("无可用输出流，编码取消");
        if (swsCtx) sws_freeContext(swsCtx);
        if (swrCtx) swr_free(&swrCtx);
        if (afifo) av_audio_fifo_free(afifo);
        return -EINVAL;
    }

    outCtx.writeHeader(ec);
    if (ec) {
        cout << String("写文件头失败: ", "Fail to write header: ") << ec.message() << endl;
        cout << String("（提示: 输出扩展名对应的容器可能不支持所选编码器组合）",
                       "(Hint: the container of the output extension may not support the selected encoders)") << endl;
        LOGD("写文件头失败: %s", ec.message().c_str());
        if (swsCtx) sws_freeContext(swsCtx);
        if (swrCtx) swr_free(&swrCtx);
        if (afifo) av_audio_fifo_free(afifo);
        return ec.value();
    }

    av::VideoFrame yuvFrame;
    int64_t aEncNextPts = 0;
    int64_t vEncNextPts = 0;
    int writtenVPkts = 0;
    int writtenAPkts = 0;
    if (videoStreamOk) {
        yuvFrame = av::VideoFrame(AV_PIX_FMT_YUV420P, outWidth, outHeight);
    }
    AVRational vInTimeBase = {0,0};
    if(videoStreamOk) vInTimeBase = fmtCtx_.raw()->streams[videoStreamIndex_]->time_base;

    cout << String("正在编码...", "Encoding...") << endl;

    auto writePkt = [&](av::Packet &pkt, const av::Stream &st, const AVRational &encTb, int64_t frameNbSamples) -> bool {
        if (!st.isValid() || !pkt) return false;
        AVPacket* rawPkt = pkt.raw();
        rawPkt->stream_index = st.index();
        if(aEnc.isOpened() && rawPkt->duration <=0){
            if(frameNbSamples>0){
                rawPkt->duration = frameNbSamples;
            }else if(aEnc.raw()->frame_size>0){
                rawPkt->duration = aEnc.raw()->frame_size;
            }
        }
        av_packet_rescale_ts(rawPkt, encTb, st.raw()->time_base);
        std::error_code writeEc;
        outCtx.writePacket(pkt, writeEc);
        if (writeEc) {
            LOGD("写包失败: %s", writeEc.message().c_str());
            return false;
        }
        return true;
    };

    auto feedVideoFrame = [&](av::VideoFrame &decFrame) {
        if (!swsCtx || !vEnc.isOpened() || !decFrame) return;
        sws_scale(swsCtx,
                  decFrame.raw()->data, decFrame.raw()->linesize,
                  0, decFrame.height(),
                  yuvFrame.raw()->data, yuvFrame.raw()->linesize);
        if (decFrame.raw()->pts != AV_NOPTS_VALUE && vInTimeBase.num > 0) {
            yuvFrame.raw()->pts = av_rescale_q(decFrame.raw()->pts, vInTimeBase, vEnc.raw()->time_base);
        } else {
            yuvFrame.raw()->pts = vEncNextPts;
        }
        vEncNextPts = yuvFrame.raw()->pts + 1;
        std::error_code encEc;
        av::Packet encPkt = vEnc.encode(yuvFrame, encEc);
        if (encEc) {
            LOGD("视频编码错误: %s", encEc.message().c_str());
            return;
        }
        if (writePkt(encPkt, outVStream, vEnc.raw()->time_base, 0)) {
            ++writtenVPkts;
        }
    };

    auto feedAudioSamples = [&](av::AudioSamples &decSamples) {
        if (!swrCtx || !aEnc.isOpened() || !decSamples) return;
        int dstNbSamples = swr_get_out_samples(swrCtx, decSamples.samplesCount());
        if (dstNbSamples <= 0) return;
        av::AudioSamples encSamples(outSampleFmt, dstNbSamples,
                                    AV_CH_LAYOUT_STEREO, adec_.sampleRate());
        int swrRet = swr_convert(swrCtx,
                                 encSamples.raw()->data, dstNbSamples,
                                 (const uint8_t**)decSamples.raw()->data,
                                 decSamples.samplesCount());
        if (swrRet < 0) return;
        if (afifo) {
            av_audio_fifo_write(afifo, (void**)encSamples.raw()->data, swrRet);
            while (av_audio_fifo_size(afifo) >= encFrameSize) {
                av::AudioSamples encFrame(outSampleFmt, encFrameSize,
                                          AV_CH_LAYOUT_STEREO, adec_.sampleRate());
                av_audio_fifo_read(afifo, (void**)encFrame.raw()->data, encFrameSize);
                encFrame.raw()->pts = aEncNextPts;
                aEncNextPts += encFrameSize;
                std::error_code encEc;
                av::Packet encPkt = aEnc.encode(encFrame, encEc);
                if (encEc) {
                    LOGD("音频编码错误: %s", encEc.message().c_str());
                    return;
                }
                if (writePkt(encPkt, outAStream, aEnc.raw()->time_base, encFrameSize)) {
                    ++writtenAPkts;
                }
            }
        } else {
            encSamples.raw()->nb_samples = swrRet;
            encSamples.raw()->pts = aEncNextPts;
            int64_t nbSmpl = swrRet;
            aEncNextPts += nbSmpl;
            std::error_code encEc;
            av::Packet encPkt = aEnc.encode(encSamples, encEc);
            if (encEc) {
                LOGD("音频编码错误: %s", encEc.message().c_str());
                return;
            }
            if (writePkt(encPkt, outAStream, aEnc.raw()->time_base, nbSmpl)) {
                ++writtenAPkts;
            }
        }
    };

    int processedPkts = 0;
    while(true){
        std::error_code readEc;
        av::Packet pkt = fmtCtx_.readPacket(readEc);
        if(readEc) { LOGD("读包结束/出错: %s", readEc.message().c_str()); break; }
        if(!pkt) break;
        bool isVideo = (videoStreamOk && pkt.streamIndex() == videoStreamIndex_);
        bool isAudio = (audioStreamOk && pkt.streamIndex() == audioStreamIndex_);
        if(!isVideo && !isAudio) continue;
        if((++processedPkts % 200) == 0){
            cout << String("进度: 已处理 ", "Progress: processed ") << processedPkts;
            cout << String(" 包", " packets") << endl;
        }
        if(isVideo && vEnc.isOpened()){
            std::error_code decEc;
            av::VideoFrame decFrame = vdec_.decode(pkt, decEc);
            if(decEc){ LOGD("视频解码错误: %s", decEc.message().c_str()); continue; }
            if(decFrame) feedVideoFrame(decFrame);
        }
        if(isAudio && swrCtx){
            std::error_code decEc;
            av::AudioSamples decSamples = adec_.decode(pkt, decEc);
            if(decEc){ LOGD("音频解码错误: %s", decEc.message().c_str()); continue; }
            if(decSamples) feedAudioSamples(decSamples);
        }
    }

    if(videoStreamOk && swsCtx){
        avcodec_send_packet(vdec_.raw(), nullptr);
        while(true){
            std::error_code decEc;
            av::VideoFrame decFrame = vdec_.decode(av::Packet(), decEc);
            if(decEc || !decFrame) break;
            feedVideoFrame(decFrame);
        }
    }
    if(audioStreamOk && swrCtx){
        avcodec_send_packet(adec_.raw(), nullptr);
        while(true){
            std::error_code decEc;
            av::AudioSamples decSamples = adec_.decode(av::Packet(), decEc);
            if(decEc || !decSamples) break;
            feedAudioSamples(decSamples);
        }
    }

    if(audioStreamOk && swrCtx){
        while(true){
            int avail = swr_get_out_samples(swrCtx,0);
            if(avail <=0) break;
            av::AudioSamples encSamples(outSampleFmt, avail,
                                        AV_CH_LAYOUT_STEREO, adec_.sampleRate());
            int swrRet = swr_convert(swrCtx, encSamples.raw()->data, avail, nullptr,0);
            if(swrRet <=0) break;
            if(afifo){
                av_audio_fifo_write(afifo, (void**)encSamples.raw()->data, swrRet);
            }else{
                encSamples.raw()->nb_samples = swrRet;
                encSamples.raw()->pts = aEncNextPts;
                int64_t nbSmpl = swrRet;
                aEncNextPts += nbSmpl;
                std::error_code encEc;
                av::Packet encPkt = aEnc.encode(encSamples, encEc);
                if (writePkt(encPkt, outAStream, aEnc.raw()->time_base, nbSmpl)) {
                    ++writtenAPkts;
                }
            }
        }
        if(afifo){
            while(av_audio_fifo_size(afifo) >0){
                int fifoSize = av_audio_fifo_size(afifo);
                int n = (fifoSize < encFrameSize) ? fifoSize : encFrameSize;
                av::AudioSamples encFrame(outSampleFmt, n,
                                          AV_CH_LAYOUT_STEREO, adec_.sampleRate());
                av_audio_fifo_read(afifo, (void**)encFrame.raw()->data, n);
                encFrame.raw()->pts = aEncNextPts;
                aEncNextPts += n;
                std::error_code encEc;
                av::Packet encPkt = aEnc.encode(encFrame, encEc);
                if (writePkt(encPkt, outAStream, aEnc.raw()->time_base, n)) {
                    ++writtenAPkts;
                }
            }
        }
    }

    if(videoStreamOk){
        int guard = 0;
        while (guard++ < 100000) {
            std::error_code encEc;
            av::Packet encPkt = vEnc.encode(encEc);
            if (encEc) break;
            if (!encPkt) break;
            if (writePkt(encPkt, outVStream, vEnc.raw()->time_base, 0)) {
                ++writtenVPkts;
            }
        }
    }
    if(audioStreamOk){
        int guard = 0;
        while (guard++ < 100000) {
            std::error_code encEc;
            av::Packet encPkt = aEnc.encode(encEc);
            if (encEc) break;
            if (!encPkt) break;
            if (writePkt(encPkt, outAStream, aEnc.raw()->time_base, aEnc.raw()->frame_size)) {
                ++writtenAPkts;
            }
        }
    }

    outCtx.writeTrailer(ec);
    // =========兜底修正音频流duration=========
    if(outAStream.isValid() && aEnc.isOpened()){
        outAStream.raw()->duration = av_rescale_q(aEncNextPts, {1,adec_.sampleRate()}, outAStream.raw()->time_base);
    }
    if (ec) {
        cout << String("写文件尾失败: ", "Fail to write trailer: ") << ec.message() << endl;
        LOGD("写文件尾失败: %s", ec.message().c_str());
    }
    cout << String("编码完成！写出视频包: ", "Encoding finished! Video packets written: ")
         << writtenVPkts
         << String(", 音频包: ", ", audio packets written: ") << writtenAPkts << endl;
    LOGD("编码完成: %s (输入包=%d, 视频包=%d, 音频包=%d)",
         outputPath, processedPkts, writtenVPkts, writtenAPkts);

    if (swsCtx) sws_freeContext(swsCtx);
    if (swrCtx) swr_free(&swrCtx);
    if (afifo) av_audio_fifo_free(afifo);
    return 0;
}

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