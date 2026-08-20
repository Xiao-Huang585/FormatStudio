//
// Created by Ding on 2025/6/8.
//

#ifndef YSPLAYER_YSAUDIOPLAYER_H
#define YSPLAYER_YSAUDIOPLAYER_H


extern  "C"{
#include "libavcodec/avcodec.h"
#include "libavformat/avformat.h"
#include <SLES/OpenSLES.h>
#include <SLES/OpenSLES_Android.h>
#include "libswresample/swresample.h"
#include "libavutil/time.h"
};
#include "YsStreamQueue.h"
#include "YsPlayerConst.h"
#include "YsCallJava.h"
#include "AndroidLog.h"

class YsAudioPlayer {
public:
    int streamIndex = 0;
    AVCodecParameters * codecParameters = nullptr;
    AVCodecContext* avCodecContext = nullptr;
    //new
    YsStreamQueue *queue = nullptr;
    pthread_t thread_play = 0;
    int sample_rate = 0;
    YsPlayerConst *playCost = nullptr;
    YsCallJava * ysCallJava = nullptr;

    SLObjectItf engineObj = nullptr;
    SLEngineItf engineEng = nullptr;

    //混音器
    SLObjectItf outputMixObj = nullptr;
    SLEnvironmentalReverbItf outputMixEnvironmentalReverb = nullptr;
    SLEnvironmentalReverbSettings reverbSettings = SL_I3DL2_ENVIRONMENT_PRESET_STONECORRIDOR;

    //pcm
    SLObjectItf pcmPlayerObj = nullptr;
    SLPlayItf pcmPlayerPlay = nullptr;

    //缓冲器队列接口
    SLAndroidSimpleBufferQueueItf pcmBufferQueue = nullptr;

    int data_size = 0 ;
    uint8_t *buffer = nullptr;

    // ====== 重采样缓存（避免每帧重建 SwrContext） ======
    SwrContext *swrCtx = nullptr;      // 缓存的重采样上下文
    AVSampleFormat cachedInFmt = AV_SAMPLE_FMT_NONE; // 上次输入采样格式
    int cachedInRate = 0;              // 上次输入采样率
    int cachedInChannels = 0;          // 上次输入声道数


    AVRational time_base; //流的时间基数
    double clock = 0; //播放进度 时长
    double currentTime = 0; //当前frame 时间
    double preTime;//上一次调用时间
    int64_t duration;

    pthread_mutex_t pauseMutex ; //暂停锁
    pthread_cond_t  pauseCond ; //暂停条件变量

    pthread_mutex_t codecMutex ; //解码锁


public:
    YsAudioPlayer(YsPlayerConst *playCost, YsCallJava *callJava);
    ~YsAudioPlayer();
    void start();

    void initOpenSLEs();

    int getPcmSampleRate(int rate);

    int resampleAudio();

    void pause();
    void resume();
    void release();

};


#endif //YSPLAYER_YSAUDIOPLAYER_H
