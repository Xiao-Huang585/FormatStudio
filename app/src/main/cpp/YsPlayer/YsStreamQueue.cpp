#include "YsStreamQueue.h"

extern "C" {
#include "libavcodec/avcodec.h"
#include "libavutil/mem.h"
}

#include <queue>
#include <pthread.h>

YsStreamQueue::YsStreamQueue() {
    pthread_mutex_init(&mutexQueue, nullptr);
    pthread_cond_init(&condQueue, nullptr);
}

YsStreamQueue::~YsStreamQueue() {
    clearQueue();
    pthread_mutex_destroy(&mutexQueue);
    pthread_cond_destroy(&condQueue);
}

void YsStreamQueue::putAvPacket(AVPacket *packet) {
    pthread_mutex_lock(&mutexQueue);
    // 队列内部保存一份拷贝，外部的packet调用方可以安全free
    AVPacket *dupPkt = av_packet_alloc();
    av_packet_ref(dupPkt, packet);
    streamQueue.push(dupPkt);
    pthread_cond_signal(&condQueue);
    pthread_mutex_unlock(&mutexQueue);
}

// 【解码线程用】阻塞版本，允许pthread_cond_wait
void YsStreamQueue::getAvPacket(AVPacket *avPacket) {
    pthread_mutex_lock(&mutexQueue);
    while(streamQueue.empty()){
        pthread_cond_wait(&condQueue,&mutexQueue);
    }
    AVPacket *pPacket = streamQueue.front();
    int ret = av_packet_ref(avPacket, pPacket);
    if(ret == 0)
    {
        streamQueue.pop();
        av_packet_free(&pPacket);
        av_free(pPacket);
    }
    pthread_mutex_unlock(&mutexQueue);
}

// 【OpenSL ES 回调线程专用】非阻塞，绝不wait！！！
bool YsStreamQueue::tryGetAvPacketNoWait(AVPacket *outPacket)
{
    pthread_mutex_lock(&mutexQueue);
    bool haveData = false;
    if (!streamQueue.empty())
    {
        AVPacket* pSrcPkt = streamQueue.front();
        int ret = av_packet_ref(outPacket, pSrcPkt);
        if (ret == 0)
        {
            streamQueue.pop();
            av_packet_free(&pSrcPkt);
            av_free(pSrcPkt);
            haveData = true;
        }
    }
    pthread_mutex_unlock(&mutexQueue);
    return haveData;
}

int YsStreamQueue::getQueueSize() {
    pthread_mutex_lock(&mutexQueue);
    int size = static_cast<int>(streamQueue.size());
    pthread_mutex_unlock(&mutexQueue);
    return size;
}

void YsStreamQueue::clearQueue() {
    pthread_mutex_lock(&mutexQueue);
    while(!streamQueue.empty()){
        AVPacket *p = streamQueue.front();
        streamQueue.pop();
        av_packet_free(&p);
        av_free(p);
    }
    pthread_mutex_unlock(&mutexQueue);
}

void YsStreamQueue::notifyQueue() {
    pthread_mutex_lock(&mutexQueue);
    pthread_cond_signal(&condQueue);
    pthread_mutex_unlock(&mutexQueue);
}
