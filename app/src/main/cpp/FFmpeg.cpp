#include "FFmpeg.h"

// 原生 FFmpeg C API（仅用于低级操作：sws/swr/metadata）
extern "C" {
#include <libavutil/pixdesc.h>
#include <libavutil/channel_layout.h>
#include <libswscale/swscale.h>
#include <libswresample/swresample.h>
#include <libavutil/opt.h>
#include <libavutil/imgutils.h>
#include <libavformat/avio.h>
}

// ============================
// 构造/析构
// ============================
FFmpeg::FFmpeg(androidOutStream &os, androidInStream &is)
        : cout(os), cin(is), outPath_("/sdcard/Download/default.mp4") {}

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
        cout << "打开文件失败: " << ec.message() << endl;
        return ec.value();
    }

    fmtCtx_.findStreamInfo(ec);
    if (ec) {
        cout << "获取流信息失败: " << ec.message() << endl;
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
        cout << "未打开输入文件..." << endl;
        LOGD("未打开输入文件...");
        return -EINVAL;
    }

    if (!hasVideo() && !hasAudio()) {
        cout << "未找到音视频流..." << endl;
        LOGD("未找到音视频流...");
        return -EINVAL;
    }

    std::error_code ec;
    outFmtCtx_.openOutput(outPath_, ec);
    if (ec) {
        cout << "打开输出文件失败..." << endl;
        LOGD("打开输出文件失败...");
        return ec.value();
    }

    // 视频初始化
    if (hasVideo() && videoID != AV_CODEC_ID_NONE) {
        av::Codec vCodec = av::findEncodingCodec(videoID);
        if (vCodec.isNull()) {
            cout << "找不到视频编码器..." << endl;
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
            cout << "打开视频编码器失败:" << ec.message() << endl;
            LOGD("打开视频编码器失败: %s", ec.message().c_str());
            return ec.value();
        }
        outVStream_ = outFmtCtx_.addStream(venc_, ec);
        if (ec) {
            cout << "创建输出视频流失败:" << ec.message() << endl;
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
            cout << "初始化像素格式失败..." << endl;
            LOGD("初始化像素格式失败...");
            return -ENOMEM;
        }

        cout << "视频编码器: " << avcodec_get_name(videoID) << '\n' << outWidth << 'x' <<
        outHeight << endl;
    }

    // 音频初始化
    if (hasAudio() && audioID != AV_CODEC_ID_NONE) {
        av::Codec aCodec = av::findEncodingCodec(audioID);
        if (aCodec.isNull()) {
            cout << "找不到音频编码器, 已取消音频..." << endl;
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
                cout << "打开音频编码器失败:" << ec.message() << endl;
                LOGD("打开音频编码器失败: %s", ec.message().c_str());
                aenc_ = av::AudioEncoderContext();
            } else {
                outAStream_ = outFmtCtx_.addStream(aenc_, ec);
                if (ec) {
                    cout << "创建输出音频流失败:" << ec.message() << endl;
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
                            cout << "初始化重采样失败..." << endl;
                            LOGD("初始化重采样失败...");
                            swr_free(&swrCtx_);
                            swrCtx_ = nullptr;
                        }
                    }
                    cout << "音频编码器: " << avcodec_get_name(audioID) << '\n' <<
                    adec_.bitRate() / 1000 << "kbps" << '\n' << adec_.sampleRate() << "Hz" << endl;
                }
            }
        }
    }
    cout << "已成功修改编码器..." << endl;
    return 0;
}

// ============================
// 获取媒体信息
// ============================
bool FFmpeg::getMediaInfo() const {
    if (!fmtCtx_.isOpened()) {
        cout << "未打开任何媒体文件" << endl;
        return false;
    }

    const AVFormatContext* rawCtx = fmtCtx_.raw();

    cout << "===== 媒体信息 =====" << endl;
    cout << "文件路径: " << url_ << endl;
    cout << "封装格式: " << (rawCtx->iformat ? rawCtx->iformat->name : "未知") << endl;

    const int64_t fileSize = avio_size(rawCtx->pb);
    if ((fileSize / (1024.0 * 1024.0 * 1024.0 * 1024.0)) > 1.5)
        cout << "文件大小(Tb):" << (fileSize / (1024.0 * 1024.0 * 1024.0 * 1024.0)) << endl;
    else if (fileSize / (1024.0 * 1024.0 * 1024.0) > 1.5)
        cout << "文件大小(Gb):" << (fileSize / (1024.0 * 1024.0 * 1024.0)) << endl;
    else if (fileSize / (1024.0 * 1024.0) > 1.5)
        cout << "文件大小(Mb):" << (fileSize / (1024.0 * 1024.0)) << endl;
    else if (fileSize / (1024.0) > 1.5)
        cout << "文件大小(Kb):" << (fileSize / (1024.0)) << endl;
    else
        cout << "文件大小(byte):" << fileSize << endl;

    if (rawCtx->duration != AV_NOPTS_VALUE) {
        const std::array<int, 4> duration =
                secondsToMicroseconds(rawCtx->duration / (AV_TIME_BASE / 1000));
        cout << "时长: " << duration[0] << ":" << duration[1] << ":"
             << duration[2] << "." << duration[3] << endl;
    } else {
        cout << "时长: 未知" << endl;
    }

    if (rawCtx->bit_rate > 0) {
        cout << "总码率: " << (rawCtx->bit_rate / 1000) << " kbps" << endl;
    } else {
        cout << "总码率: 未知" << endl;
    }

    cout << "流的数量: " << fmtCtx_.streamsCount() << endl;

    if (hasVideo()) {
        AVStream* vStream = rawCtx->streams[videoStreamIndex_];
        AVRational frameRate = vStream->avg_frame_rate;

        cout << "\n[视频流]" << endl;
        cout << "  流索引: " << videoStreamIndex_ << endl;
        cout << "  分辨率: " << vdec_.width() << "x" << vdec_.height() << endl;

        const char* pixName = av_get_pix_fmt_name(vdec_.pixelFormat());
        cout << "  像素格式: " << (pixName ? pixName : "未知") << endl;

        if (frameRate.den > 0 && frameRate.num > 0) {
            cout << "  帧率: " << av_q2d(frameRate) << " fps" << endl;
        }

        AVCodecParameters* codecpar = vStream->codecpar;
        cout << "  编码器: " << avcodec_get_name(codecpar->codec_id) << endl;
        if (codecpar->bit_rate > 0) {
            cout << "  码率: " << (codecpar->bit_rate / 1000) << " kbps" << endl;
        }
    }

    if (hasAudio()) {
        AVStream* aStream = rawCtx->streams[audioStreamIndex_];

        cout << "\n[音频流]" << endl;
        cout << "  流索引: " << audioStreamIndex_ << endl;
        cout << "  采样率: " << adec_.sampleRate() << " Hz" << endl;

        char chLayoutDesc[256] = {0};
        av_channel_layout_describe(&adec_.raw()->ch_layout, chLayoutDesc, sizeof(chLayoutDesc));
        cout << "  声道布局: " << chLayoutDesc << endl;

        AVCodecParameters* codecpar = aStream->codecpar;
        cout << "  编码器: " << avcodec_get_name(codecpar->codec_id) << endl;

        if (codecpar->bit_rate > 0) {
            cout << "  码率: " << (codecpar->bit_rate / 1000) << " kbps" << endl;
        }

        AVDictionaryEntry* entry = nullptr;
        entry = av_dict_get(rawCtx->metadata, "artist", nullptr, 0);
        if (entry && entry->value)
            cout << "  艺术家: " << entry->value << endl;
        entry = av_dict_get(rawCtx->metadata, "album", nullptr, 0);
        if (entry && entry->value)
            cout << "  专辑: " << entry->value << endl;
        entry = av_dict_get(rawCtx->metadata, "encoder", nullptr, 0);
        if (entry && entry->value)
            cout << "  编码器: " << entry->value << endl;
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

    // 2. 视频编码器初始化
    av::VideoEncoderContext vEnc;
    av::Stream outVStream;
    SwsContext* swsCtx = nullptr;
    int outWidth = 0, outHeight = 0;

    if (hasVideo()) {
        av::Codec vCodec = av::findEncodingCodec(AV_CODEC_ID_H264);
        if (!vCodec.isNull()) {
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

    if (hasAudio()) {
        av::Codec aCodec = av::findEncodingCodec(AV_CODEC_ID_AAC);
        if (aCodec.isNull()) {
            aEnc = av::AudioEncoderContext(aCodec);

            aEnc.setSampleRate(adec_.sampleRate());
            aEnc.setSampleFormat(AV_SAMPLE_FMT_FLTP);
            aEnc.setChannels(2);
            aEnc.setChannelLayout(AV_CH_LAYOUT_STEREO);
            aEnc.setBitRate(128 * 1000);
            aEnc.setTimeBase(av::Rational(1, adec_.sampleRate()));

            aEnc.open(ec);
            if (ec) {
                cout << "打开音频编码器失败: " << ec.message() << endl;
                aEnc = av::AudioEncoderContext();
            } else {
                outAStream = outCtx.addStream(aEnc, ec);
                if (ec) {
                    cout << "创建输出音频流失败: " << ec.message() << endl;
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

                    if (swrCtx && swrRet >= 0) {
                        swrRet = swr_init(swrCtx);
                        if (swrRet < 0) {
                            cout << "初始化重采样失败" << endl;
                            swr_free(&swrCtx);
                            swrCtx = nullptr;
                        }
                    }
                    cout << "音频编码: AAC 128kbps" << endl;
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
    int64_t aEncNextPts = 0;

    if (hasVideo() && vEnc.isOpened()) {
        yuvFrame = av::VideoFrame(AV_PIX_FMT_YUV420P, outWidth, outHeight);
    }

    // 获取时间基
    AVRational vInTimeBase = {0, 0};
    if (hasVideo()) {
        vInTimeBase = fmtCtx_.raw()->streams[videoStreamIndex_]->time_base;
    }

    cout << "正在压缩..." << endl;

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
            if (!decFrame) {
                continue;
            }

            // 像素格式转换（通过 raw() 访问底层 AVFrame）
            sws_scale(swsCtx,
                      decFrame.raw()->data, decFrame.raw()->linesize,
                      0, decFrame.height(),
                      yuvFrame.raw()->data, yuvFrame.raw()->linesize);

            // 时间戳
            AVRational vOutTb = vEnc.raw()->time_base;
            yuvFrame.raw()->pts = av_rescale_q(decFrame.raw()->pts, vInTimeBase, vOutTb);

            // 编码
            std::error_code encEc;
            av::Packet encPkt = vEnc.encode(yuvFrame, encEc);
            while (encPkt) {
                if (outVStream.isValid()) {
                    encPkt.raw()->stream_index = outVStream.index();
                    AVRational outStreamTb = outVStream.raw()->time_base;
                    av_packet_rescale_ts(encPkt.raw(), vOutTb, outStreamTb);
                    std::error_code writeEc;
                    outCtx.writePacket(encPkt, writeEc);
                }
                encPkt = vEnc.encode(encEc);
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
            if (!decSamples) {
                continue;
            }

            // 重采样
            int dstNbSamples = swr_get_out_samples(swrCtx, decSamples.samplesCount());
            av::AudioSamples encSamples(
                    AV_SAMPLE_FMT_FLTP, dstNbSamples,
                    AV_CH_LAYOUT_STEREO, adec_.sampleRate());

            int swrRet = swr_convert(swrCtx,
                                     encSamples.raw()->data, dstNbSamples,
                                     (const uint8_t**)decSamples.raw()->data,
                                     decSamples.samplesCount());
            if (swrRet < 0) continue;

            encSamples.raw()->pts = aEncNextPts;
            aEncNextPts += dstNbSamples;

            // 编码
            std::error_code encEc;
            av::Packet encPkt = aEnc.encode(encSamples, encEc);
            while (encPkt) {
                if (outAStream.isValid()) {
                    encPkt.raw()->stream_index = outAStream.index();
                    AVRational aEncTb = aEnc.raw()->time_base;
                    AVRational outStreamTb = outAStream.raw()->time_base;
                    av_packet_rescale_ts(encPkt.raw(), aEncTb, outStreamTb);
                    std::error_code writeEc;
                    outCtx.writePacket(encPkt, writeEc);
                }
                encPkt = aEnc.encode(encEc);
            }
        }
    }

    // 7. 刷新视频编码器
    if (vEnc.isOpened()) {
        std::error_code encEc;
        av::Packet encPkt = vEnc.encode(encEc);
        while (encPkt) {
            if (outVStream.isValid()) {
                encPkt.raw()->stream_index = outVStream.index();
                AVRational vOutTb = vEnc.raw()->time_base;
                AVRational outStreamTb = outVStream.raw()->time_base;
                av_packet_rescale_ts(encPkt.raw(), vOutTb, outStreamTb);
                std::error_code writeEc;
                outCtx.writePacket(encPkt, writeEc);
            }
            encPkt = vEnc.encode(encEc);
        }
    }

    // 8. 刷新音频编码器
    if (aEnc.isOpened()) {
        // 先 flush 重采样器
        if (swrCtx) {
            int dstNbSamples = swr_get_out_samples(swrCtx, 0);
            if (dstNbSamples > 0) {
                av::AudioSamples encSamples(
                        AV_SAMPLE_FMT_FLTP, dstNbSamples,
                        AV_CH_LAYOUT_STEREO, adec_.sampleRate());

                swr_convert(swrCtx,
                            encSamples.raw()->data, dstNbSamples,
                            nullptr, 0);

                encSamples.raw()->pts = aEncNextPts;
                std::error_code encEc;
                aEnc.encode(encSamples, encEc);
            }
        }

        std::error_code encEc;
        av::Packet encPkt = aEnc.encode(encEc);
        while (encPkt) {
            if (outAStream.isValid()) {
                encPkt.raw()->stream_index = outAStream.index();
                AVRational aEncTb = aEnc.raw()->time_base;
                AVRational outStreamTb = outAStream.raw()->time_base;
                av_packet_rescale_ts(encPkt.raw(), aEncTb, outStreamTb);
                std::error_code writeEc;
                outCtx.writePacket(encPkt, writeEc);
            }
            encPkt = aEnc.encode(encEc);
        }
    }

    // 9. 写文件尾
    outCtx.writeTrailer(ec);

    cout << "压缩完成！" << endl;

    // 10. 释放资源（avcpp 对象通过 RAII 自动释放，仅清理 C API 资源）
    if (swsCtx) sws_freeContext(swsCtx);
    if (swrCtx) swr_free(&swrCtx);

    return 0;
}
