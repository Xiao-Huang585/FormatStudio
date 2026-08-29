#ifndef YSSTREAMQUEUE_H
#define YSSTREAMQUEUE_H

#include <queue>
#include <pthread.h>

extern "C" {
#include "libavcodec/avcodec.h"
}

class YsStreamQueue {
public:
    YsStreamQueue();
    ~YsStreamQueue();

    void putAvPacket(AVPacket *packet);
    void getAvPacket(AVPacket *avPacket);       // 解码线程：阻塞版本
    bool tryGetAvPacketNoWait(AVPacket *outPacket); // OpenSL回调：非阻塞，绝不wait

    int getQueueSize();
    void clearQueue();
    void notifyQueue();

    pthread_mutex_t mutexQueue;
    pthread_cond_t condQueue;
private:
    std::queue<AVPacket*> streamQueue;
};

#endif //YSSTREAMQUEUE_H
