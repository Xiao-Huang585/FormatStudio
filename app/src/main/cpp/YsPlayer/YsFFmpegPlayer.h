//
// Created by Ding on 2025/6/6.
//

#ifndef YSPLAYER_YSFFMPEGPLAYER_H
#define YSPLAYER_YSFFMPEGPLAYER_H
#include "pthread.h"
extern  "C"{
#include "libavcodec/avcodec.h"
#include "libavformat/avformat.h"
};
#include "AndroidLog.h"
#include "YsAudioPlayer.h"
#include "YsVideoPlayer.h"
#include "YsPlayerConst.h"
#include "YsCallJava.h"

class YsFFmpegPlayer {
public:
    pthread_t initDecodeThread = 0;
    const char* url ;
    YsAudioPlayer * ysAudioPlayer = nullptr;
    YsVideoPlayer * ysVideoPlayer = nullptr;
    YsPlayerConst * ysPlayerConst = nullptr;
    AVFormatContext * avFormatContext = nullptr;
    YsCallJava * callJava = nullptr;
    pthread_t startDecodeThread;
    pthread_mutex_t seek_mutex;
    bool  enalbeMediaCodec = true; //是否启用硬解码

public:
    YsFFmpegPlayer(YsCallJava *pJava);
    ~YsFFmpegPlayer();
    void prepare(const char* url);
    void ffmpegDecode();

    void start();
    void pause();
    void resume();
    void ffmpegStart();
    void release();
    void createHwDecode();

    void seek(int i);

    void enableMediaCodec(bool b);
};


#endif //YSPLAYER_YSFFMPEGPLAYER_H
