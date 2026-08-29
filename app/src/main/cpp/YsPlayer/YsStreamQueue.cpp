#include "YsStreamQueue.h"

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
    streamQueue.push(packet);
    pthread_cond_signal(&condQueue);
    pthread_mutex_unlock(&mutexQueue);
}

// 阻塞版本，给解码线程 ffmpegStart 使用，允许wait
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

// 非阻塞，OpenSL‑ES回调线程调用，禁止wait
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
