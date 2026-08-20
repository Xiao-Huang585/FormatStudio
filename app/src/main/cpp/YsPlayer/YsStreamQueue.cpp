//
// Created by Ding on 2025/6/8.
//

#include "YsStreamQueue.h"


YsStreamQueue::YsStreamQueue() {
    pthread_mutex_init(&mutexQueue, NULL);
    pthread_cond_init(&condQueue, NULL);
}

YsStreamQueue::~YsStreamQueue() {
    clearQueue();
    pthread_mutex_destroy(&mutexQueue);
    pthread_cond_destroy(&condQueue);
}

void YsStreamQueue::getAvPacket(AVPacket *avPacket) {
    pthread_mutex_lock(&mutexQueue);
    if(streamQueue.size()>0){
        AVPacket *pPacket = streamQueue.front();
        if(av_packet_ref(avPacket,pPacket)==0){
            streamQueue.pop();
        }
        av_packet_free(&pPacket);
        av_free(pPacket);
        pPacket = NULL;
    }else{
        pthread_cond_wait(&condQueue,&mutexQueue);
    }
    pthread_mutex_unlock(&mutexQueue);
    
}

void YsStreamQueue::putAvPacket(AVPacket *avPacket) {
    pthread_mutex_lock(&mutexQueue);
    streamQueue.push(avPacket);
    pthread_cond_signal(&condQueue);
    pthread_mutex_unlock(&mutexQueue);
}

int YsStreamQueue::getQueueSize() {
    int size = 0;
    pthread_mutex_lock(&mutexQueue);
    size = streamQueue.size();
    pthread_mutex_unlock(&mutexQueue);
    return size;
}

void YsStreamQueue::clearQueue() {
    pthread_mutex_lock(&mutexQueue);
    while (!streamQueue.empty()) {
        AVPacket *pPacket = streamQueue.front();
        av_packet_free(&pPacket);
        av_free(pPacket);
        streamQueue.pop();
    }
    pthread_mutex_unlock(&mutexQueue);
}

void YsStreamQueue::notifyQueue() {
    pthread_cond_signal(&condQueue);
}
