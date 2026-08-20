//
// Created by Ding on 2025/6/8.
//

#ifndef YSPLAYER_YSVIDEOPLAYER_H
#define YSPLAYER_YSVIDEOPLAYER_H

extern  "C"{
#include "libavcodec/avcodec.h"
#include "libavformat/avformat.h"
#include "libavutil/time.h"
#include "libavutil/imgutils.h"
#include "libswscale/swscale.h"
};
#include "YsStreamQueue.h"
#include "YsPlayerConst.h"
#include "pthread.h"
#include "AndroidLog.h"
#include "YsCallJava.h"
#include "YsAudioPlayer.h"
class YsVideoPlayer {
public :
    int streamIndex = 0;
    AVCodecParameters * codecParameters = nullptr;
    AVCodecContext* avCodecContext = nullptr;
    YsStreamQueue* queue = nullptr;
    YsPlayerConst * playerConst = nullptr;
    YsCallJava * callJava = nullptr;
    double defauleDelayTime = 0;
    pthread_t  videoDecodeThread;
    YsAudioPlayer * ysAudioPlayer = nullptr;
    AVRational time_base; //流的时间基数
    pthread_mutex_t pauseMutex ; //暂停锁
    pthread_cond_t  pauseCond ; //暂停条件变量

    pthread_mutex_t codecMutex ; //暂停锁

    // ====== 渲染缓存（避免每帧重建，大幅提升性能） ======
    SwsContext *swsCtx = nullptr;       // 缓存的 YUV→RGBA 转换上下文
    AVFrame *rgbaFrame = nullptr;       // 缓存的 RGBA 帧（复用内存）
    int cachedWidth = 0;               // 上次渲染宽度（判断是否需重建 swsCtx）
    int cachedHeight = 0;              // 上次渲染高度
    AVPixelFormat cachedFormat = AV_PIX_FMT_NONE; // 上次像素格式

public:
    YsVideoPlayer(YsPlayerConst *ysPlayerConst, YsCallJava *pJava,YsAudioPlayer * ysAudioPlayer);
    ~YsVideoPlayer();
    void start();
    void convertNV12toYUV420P(AVFrame *pFrame, AVFrame *pFrame1, int width, int height);
    void convertNV12toRGB24(AVFrame *pFrame, AVFrame *pFrame1, int width, int height);
    double syncClock(AVFrame *packet);
    void pause();
    void resume();
    void stop();
    void release();

    void copyFrameData(uint8_t *src, uint8_t *dst, int linesize, int width, int height);
};


#endif //YSPLAYER_YSVIDEOPLAYER_H
