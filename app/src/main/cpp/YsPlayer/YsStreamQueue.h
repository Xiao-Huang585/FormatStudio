//
// Created by Ding on 2025/6/8.
//

#ifndef YSPLAYER_YSSTREAMQUEUE_H
#define YSPLAYER_YSSTREAMQUEUE_H

#include "queue"
#include "pthread.h"
extern  "C"{
#include "libavcodec/avcodec.h"
#include "libavformat/avformat.h"
};

class YsStreamQueue {
public:
    std::queue<AVPacket*> streamQueue;
    pthread_mutex_t  mutexQueue;
    pthread_cond_t   condQueue;

public:
    YsStreamQueue();
    ~YsStreamQueue();

    void getAvPacket(AVPacket* avPacket);
    void putAvPacket(AVPacket* avPacket);
    int getQueueSize();
    void clearQueue();
    void notifyQueue();
};


#endif //YSPLAYER_YSSTREAMQUEUE_H
