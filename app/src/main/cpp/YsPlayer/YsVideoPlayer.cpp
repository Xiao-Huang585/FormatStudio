//
// Created by Ding on 2025/6/8.
//

#include "YsVideoPlayer.h"
#include "YsCallJava.h"
#include "libavcodec/jni.h"

// 引入全局 ANativeWindow（由 native-lib.cpp 中的 setNativeWindow 设置）
#include <android/native_window.h>
#include <android/native_window_jni.h>

// 声明外部函数（定义在 functions.cpp 中）
// acquireNativeWindow 增加引用计数，使用后必须调用 releaseAcquiredNativeWindow
ANativeWindow *acquireNativeWindow();
void releaseAcquiredNativeWindow(ANativeWindow *window);

int count = 0;
int meidaCodecHandle = 0;
YsVideoPlayer::YsVideoPlayer(YsPlayerConst *ysPlayerConst, YsCallJava *pJava,YsAudioPlayer * ysAudioPlayer) {
    queue = new YsStreamQueue();
    this->playerConst = ysPlayerConst;
    this->callJava = pJava;
    this->ysAudioPlayer = ysAudioPlayer;
    pthread_mutex_init(&pauseMutex, NULL);
    pthread_cond_init(&pauseCond, NULL);

    pthread_mutex_init(&codecMutex, NULL);
}

YsVideoPlayer::~YsVideoPlayer() {
    pthread_mutex_destroy(&pauseMutex);
    pthread_cond_destroy(&pauseCond);
    pthread_mutex_destroy(&codecMutex);
}

void *playVideo(void *ctx) {
    YsVideoPlayer *ysVideoPlayer = static_cast<YsVideoPlayer *>(ctx);
    while (!ysVideoPlayer->playerConst->exit) {
        if(ysVideoPlayer->playerConst->pause){
            pthread_mutex_lock(&ysVideoPlayer->pauseMutex);
            pthread_cond_wait(&ysVideoPlayer->pauseCond, &ysVideoPlayer->pauseMutex);
            pthread_mutex_unlock(&ysVideoPlayer->pauseMutex);
        }
        if(ysVideoPlayer->playerConst->seek){
            av_usleep(1000 * 100);
            continue;
        }
        if (ysVideoPlayer->queue->getQueueSize() == 0) {
            av_usleep(100 * 1000);
            continue;
        }

        AVPacket *pPacket = av_packet_alloc();
        ysVideoPlayer->queue->getAvPacket(pPacket);
        pthread_mutex_lock(&ysVideoPlayer->codecMutex);

        // ================================================================
        // 1. 先发送 packet 到解码器（正确顺序！）
        //    原代码先 receive 再 send，导致解码器永远处理上一帧的数据
        //    管线永远不满载，效率极低
        // ================================================================
        if(count%3==0&&meidaCodecHandle<10){
            meidaCodecHandle++;
        }
        if (avcodec_send_packet(ysVideoPlayer->avCodecContext, pPacket) != 0) {
            av_packet_free(&pPacket);
            pthread_mutex_unlock(&ysVideoPlayer->codecMutex);
            continue;
        }
        if(meidaCodecHandle<10){
            count++;
        }
        av_packet_free(&pPacket);

        // ================================================================
        // 2. 接收解码后的帧
        //    关键优化：
        //    a) 帧丢弃：如果帧落后于音频时钟，直接跳过不渲染
        //    b) 只渲染最后一帧：多帧堆积时只渲染最新的一帧
        //    c) sleep 移到循环外：避免每帧都 sleep
        // ================================================================
        AVFrame *renderFrame = nullptr;  // 待渲染的帧
        bool needSleep = false;

        while (true) {
            AVFrame *pFrame = av_frame_alloc();
            int rel = avcodec_receive_frame(ysVideoPlayer->avCodecContext, pFrame);
            if (rel != 0) {
                av_frame_free(&pFrame);
                break;
            }

            // 帧同步检查：如果视频落后音频太多，丢弃此帧
            double videoClock = pFrame->pts * av_q2d(ysVideoPlayer->time_base);
            double audioClock = ysVideoPlayer->ysAudioPlayer->clock;
            double diff = audioClock - videoClock;

            if (diff > 0.1) {
                // 视频落后超过 100ms，丢弃此帧追赶
                av_frame_free(&pFrame);
                continue;
            }

            // 保留最后一帧（丢弃前面已过时的帧）
            if (renderFrame) {
                av_frame_free(&renderFrame);
            }
            renderFrame = pFrame;
            needSleep = true;
        }

        // ================================================================
        // 3. 渲染最后一帧 + 帧率控制 sleep
        // ================================================================
        if (renderFrame) {
            // 帧率控制：sleep 在循环外，只 sleep 一次
            av_usleep(ysVideoPlayer->syncClock(renderFrame) * 1000000);

            int videoWidth = renderFrame->width;
            int videoHeight = renderFrame->height;
            AVPixelFormat srcFormat = (AVPixelFormat)renderFrame->format;

            // 跳过无效格式帧
            if (srcFormat != AV_PIX_FMT_NONE && videoWidth > 0 && videoHeight > 0) {

                // 缓存 SwsContext（仅格式/尺寸变化时重建）
                if (ysVideoPlayer->swsCtx == nullptr ||
                    ysVideoPlayer->cachedWidth != videoWidth ||
                    ysVideoPlayer->cachedHeight != videoHeight ||
                    ysVideoPlayer->cachedFormat != srcFormat) {

                    if (ysVideoPlayer->swsCtx) {
                        sws_freeContext(ysVideoPlayer->swsCtx);
                        ysVideoPlayer->swsCtx = nullptr;
                    }
                    if (ysVideoPlayer->rgbaFrame) {
                        av_freep(&ysVideoPlayer->rgbaFrame->data[0]);
                        av_frame_free(&ysVideoPlayer->rgbaFrame);
                        ysVideoPlayer->rgbaFrame = nullptr;
                    }

                    // 使用 SWS_FAST_BILINEAR（比 SWS_BILINEAR 快很多，质量差异极小）
                    ysVideoPlayer->swsCtx = sws_getContext(
                            videoWidth, videoHeight, srcFormat,
                            videoWidth, videoHeight, AV_PIX_FMT_RGBA,
                            SWS_FAST_BILINEAR, nullptr, nullptr, nullptr);

                    if (ysVideoPlayer->swsCtx) {
                        ysVideoPlayer->rgbaFrame = av_frame_alloc();
                        if (av_image_alloc(ysVideoPlayer->rgbaFrame->data,
                                           ysVideoPlayer->rgbaFrame->linesize,
                                           videoWidth, videoHeight,
                                           AV_PIX_FMT_RGBA, 1) <= 0) {
                            av_frame_free(&ysVideoPlayer->rgbaFrame);
                            ysVideoPlayer->rgbaFrame = nullptr;
                        }
                        ysVideoPlayer->cachedWidth = videoWidth;
                        ysVideoPlayer->cachedHeight = videoHeight;
                        ysVideoPlayer->cachedFormat = srcFormat;
                    }
                }

                // YUV → RGBA 转换 + 渲染
                if (ysVideoPlayer->swsCtx && ysVideoPlayer->rgbaFrame) {
                    sws_scale(ysVideoPlayer->swsCtx,
                              renderFrame->data, renderFrame->linesize, 0, videoHeight,
                              ysVideoPlayer->rgbaFrame->data,
                              ysVideoPlayer->rgbaFrame->linesize);

                    ANativeWindow *window = acquireNativeWindow();
                    if (window != nullptr) {
                        ANativeWindow_setBuffersGeometry(window,
                                                         videoWidth, videoHeight,
                                                         WINDOW_FORMAT_RGBA_8888);

                        ANativeWindow_Buffer windowBuffer;
                        if (ANativeWindow_lock(window, &windowBuffer, nullptr) == 0) {
                            uint8_t *dst = (uint8_t *)windowBuffer.bits;
                            uint8_t *src = ysVideoPlayer->rgbaFrame->data[0];
                            int dstStride = windowBuffer.stride * 4;
                            int srcStride = ysVideoPlayer->rgbaFrame->linesize[0];

                            // 优化：stride 相同时单次 memcpy（避免逐行循环）
                            if (dstStride == srcStride) {
                                memcpy(dst, src, (size_t)dstStride * videoHeight);
                            } else {
                                for (int y = 0; y < videoHeight; y++) {
                                    memcpy(dst + y * dstStride, src + y * srcStride,
                                           (size_t)videoWidth * 4);
                                }
                            }
                            ANativeWindow_unlockAndPost(window);
                        }
                        releaseAcquiredNativeWindow(window);
                    }
                }
            }

            av_frame_free(&renderFrame);
        }

        pthread_mutex_unlock(&ysVideoPlayer->codecMutex);
    }
    return nullptr;
}

void YsVideoPlayer::copyFrameData(uint8_t *src, uint8_t *dst, int linesize, int width, int height) {
    width = FFMIN(linesize, width);
    memset(dst, 0, width * height);
    for (int i = 0; i < height; ++i) {
        memcpy(dst, src, width);
        dst += width;
        src += linesize;
    }
}
void YsVideoPlayer::start() {
    pthread_create(&videoDecodeThread, NULL, playVideo, this);
}

void YsVideoPlayer::convertNV12toRGB24(AVFrame *nv12, AVFrame *rgb24, int width, int height) {
    SwsContext *sws_ctx = sws_getContext(
            width, height, (enum AVPixelFormat)nv12->format,
            width, height, AV_PIX_FMT_RGB24,
            SWS_BILINEAR, nullptr, nullptr, nullptr);

    if (!sws_ctx) {
        LOGD("sws_getContext 创建失败，无法转换为 rgb24");
        return;
    }
    sws_setColorspaceDetails(
            sws_ctx,
            sws_getCoefficients(SWS_CS_ITU601),
            0,
            sws_getCoefficients(SWS_CS_ITU709),
            0,
            0, 1 << 16, 1 << 16
    );
    sws_scale(
            sws_ctx,
            nv12->data, nv12->linesize,
            0, height,
            rgb24->data, rgb24->linesize
    );

    sws_freeContext(sws_ctx);
}
void YsVideoPlayer::convertNV12toYUV420P(AVFrame *nv12, AVFrame *yuv420p, int width, int height) {
    int y_size = width * height;
    LOGD("yuv 420p %p",yuv420p->data[0])

    if(width == nv12->linesize[0]){
        memcpy(yuv420p->data[0], nv12->data[0], y_size);
    }else{
        for(int y = 0;y< height;y++){
            for(int x = 0 ;x<width;x++){
                yuv420p->data[0][y*yuv420p->linesize[0]+x] = nv12->data[0][y*nv12->linesize[0]+x];
            }
        }
    }
    for (int i = 0; i < height / 2; i++) {
        for (int j = 0; j < width / 2; j++) {
            yuv420p->data[1][i*yuv420p->linesize[1]+j] = nv12->data[1][i*nv12->linesize[1]+2*j];     // U
            yuv420p->data[2][i*yuv420p->linesize[2]+j] = nv12->data[1][i*nv12->linesize[1]+2*j+1];     // V
        }
    }
}

double YsVideoPlayer::syncClock(AVFrame *packet) {
    double videoClock = packet->pts* av_q2d(time_base);
    double diff = ysAudioPlayer->clock - videoClock;
    double delay = defauleDelayTime;
    double sync_threshold = FFMAX(0.04,delay);
    if (diff <= -sync_threshold )
        delay = FFMIN(delay + (-diff),0.1);
    else if (diff >= sync_threshold)
        delay = FFMAX(0.01,delay - diff);
    return delay;
}

void YsVideoPlayer::resume() {
    pthread_mutex_lock(&pauseMutex);
    pthread_cond_signal(&pauseCond);
    pthread_mutex_unlock(&pauseMutex);
}

void YsVideoPlayer::release() {
    if(queue){
        queue->notifyQueue();
    }
    resume();
    LOGD("停止视频解码线程");
    if (videoDecodeThread) {
        pthread_join(videoDecodeThread, nullptr);
    }
    LOGD("释放渲染缓存");
    // 释放缓存的 SwsContext 和 rgbaFrame
    if (swsCtx) {
        sws_freeContext(swsCtx);
        swsCtx = nullptr;
    }
    if (rgbaFrame) {
        av_freep(&rgbaFrame->data[0]);
        av_frame_free(&rgbaFrame);
        rgbaFrame = nullptr;
    }
    cachedWidth = 0;
    cachedHeight = 0;
    cachedFormat = AV_PIX_FMT_NONE;

    LOGD("停止avcodec");
    if (avCodecContext) {
        // FFmpeg 5.x: avcodec_close 已移除，avcodec_free_context 会自动关闭
        avcodec_free_context(&avCodecContext);
        avCodecContext = nullptr;
    }
    LOGD("停止queue");
    if (queue) {
        delete queue;
        queue = nullptr;
    }
}
