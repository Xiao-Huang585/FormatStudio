//
// Created by Ding on 2025/6/8.
//

#include "YsAudioPlayer.h"

YsAudioPlayer::YsAudioPlayer(YsPlayerConst *playCost, YsCallJava *callJava) {
    this->playCost = playCost;
    this->ysCallJava = callJava;
    queue = new YsStreamQueue();

    pthread_mutex_init(&pauseMutex, NULL);
    pthread_cond_init(&pauseCond, NULL);
    pthread_mutex_init(&codecMutex, NULL);
}
YsAudioPlayer::~YsAudioPlayer() {
    pthread_mutex_destroy(&pauseMutex);
    pthread_cond_destroy(&pauseCond);
    pthread_mutex_destroy(&codecMutex);
}
void *startDecodeThread(void *ctx) {
    YsAudioPlayer *audioPlayer = (YsAudioPlayer *) ctx;
    audioPlayer->initOpenSLEs();
    return nullptr;
}
void YsAudioPlayer::start() {
    //buffer = 采样率（sample_rate）*采样位数（16 bit/8bit =2 byte） *声道数（2）
    buffer = static_cast<uint8_t *>(av_malloc(sample_rate * 2 * 2));
    pthread_create(&thread_play, nullptr, startDecodeThread, this);
}


void pcmBufferCallBack(SLAndroidSimpleBufferQueueItf pItf, void *ctx) {
    YsAudioPlayer *ysAudioPlayer = (YsAudioPlayer *) ctx;
    if (ysAudioPlayer != NULL) {
        int buffersize = ysAudioPlayer->resampleAudio();
        if (buffersize > 0) {
//            ysAudioPlayer->clock += buffersize / ((double) (ysAudioPlayer->sample_rate * 2 * 2));
            if (ysAudioPlayer->clock - ysAudioPlayer->preTime >= 0.5) { //间隔多久回调 0.1秒 不能 太长 太长 最后一秒回调不了
                ysAudioPlayer->preTime = ysAudioPlayer->clock;
                //回调应用层
                ysAudioPlayer->ysCallJava->onCallTimeInfo(ysAudioPlayer->clock, ysAudioPlayer->duration);
            }
            (*ysAudioPlayer->pcmBufferQueue)->Enqueue(ysAudioPlayer->pcmBufferQueue, (char *) ysAudioPlayer->buffer,
                                                      buffersize);
        }
    }
}

void YsAudioPlayer::initOpenSLEs() {
    SLresult result;
    slCreateEngine(&engineObj, 0, nullptr, 0, nullptr, nullptr);
    (*engineObj)->Realize(engineObj, SL_BOOLEAN_FALSE);
    (*engineObj)->GetInterface(engineObj, SL_IID_ENGINE, &engineEng);
    //创建混音器
    const SLInterfaceID mids[1] = {SL_IID_ENVIRONMENTALREVERB};
    const SLboolean mreq[1] = {SL_BOOLEAN_FALSE};
    result = (*engineEng)->CreateOutputMix(engineEng, &outputMixObj, 1, mids, mreq);
    (void) result;
    result = (*outputMixObj)->Realize(outputMixObj, SL_BOOLEAN_FALSE);
    (void) result;
    result = (*outputMixObj)->GetInterface(outputMixObj, SL_IID_ENVIRONMENTALREVERB,
                                           &outputMixEnvironmentalReverb);
    if (SL_RESULT_SUCCESS == result) {
        result = (*outputMixEnvironmentalReverb)->SetEnvironmentalReverbProperties(
                outputMixEnvironmentalReverb, &reverbSettings);
        (void) result;
    }
    SLDataLocator_OutputMix outputMix = {SL_DATALOCATOR_OUTPUTMIX,
                                         outputMixObj};
    SLDataSink audioSnk = {&outputMix, 0};

    SLDataLocator_AndroidSimpleBufferQueue android_queue = {SL_DATALOCATOR_ANDROIDSIMPLEBUFFERQUEUE,
                                                            2};
    SLDataFormat_PCM pcm = {
            SL_DATAFORMAT_PCM,//播放pcm格式的数据
            2,//2个声道（立体声）
            static_cast<SLuint32>(getPcmSampleRate(sample_rate)),
            SL_PCMSAMPLEFORMAT_FIXED_16,//位数 16位
            SL_PCMSAMPLEFORMAT_FIXED_16,//和位数一致就行
            SL_SPEAKER_FRONT_LEFT | SL_SPEAKER_FRONT_RIGHT,//立体声（前左前右）
            SL_BYTEORDER_LITTLEENDIAN//结束标志
    };
    SLDataSource slDataSource = {&android_queue, &pcm};

    const SLInterfaceID ids[2] = {SL_IID_BUFFERQUEUE, SL_IID_PLAYBACKRATE};
    const SLboolean req[2] = {SL_BOOLEAN_TRUE, SL_BOOLEAN_TRUE};
    (*engineEng)->CreateAudioPlayer(engineEng, &pcmPlayerObj, &slDataSource, &audioSnk, 2,
                                    ids, req);
    //初始化播放器
    (*pcmPlayerObj)->Realize(pcmPlayerObj, SL_BOOLEAN_FALSE);
//    得到接口后调用  获取Player接口
    (*pcmPlayerObj)->GetInterface(pcmPlayerObj, SL_IID_PLAY, &pcmPlayerPlay);
//    注册回调缓冲区 获取缓冲队列接口
    (*pcmPlayerObj)->GetInterface(pcmPlayerObj, SL_IID_BUFFERQUEUE, &pcmBufferQueue);
    //缓冲接口回调
    (*pcmBufferQueue)->RegisterCallback(pcmBufferQueue, pcmBufferCallBack, this);
//    获取播放状态接口
    (*pcmPlayerPlay)->SetPlayState(pcmPlayerPlay, SL_PLAYSTATE_PLAYING);
    pcmBufferCallBack(pcmBufferQueue, this); //回调方法 内 开始播放数据
}

int YsAudioPlayer::getPcmSampleRate(int sample_rate) {
    int rate = 0;
    switch (sample_rate) {
        case 8000:
            rate = SL_SAMPLINGRATE_8;
            break;
        case 11025:
            rate = SL_SAMPLINGRATE_11_025;
            break;
        case 12000:
            rate = SL_SAMPLINGRATE_12;
            break;
        case 16000:
            rate = SL_SAMPLINGRATE_16;
            break;
        case 22050:
            rate = SL_SAMPLINGRATE_22_05;
            break;
        case 24000:
            rate = SL_SAMPLINGRATE_24;
            break;
        case 32000:
            rate = SL_SAMPLINGRATE_32;
            break;
        case 44100:
            rate = SL_SAMPLINGRATE_44_1;
            break;
        case 48000:
            rate = SL_SAMPLINGRATE_48;
            break;
        case 64000:
            rate = SL_SAMPLINGRATE_64;
            break;
        case 88200:
            rate = SL_SAMPLINGRATE_88_2;
            break;
        case 96000:
            rate = SL_SAMPLINGRATE_96;
            break;
        case 192000:
            rate = SL_SAMPLINGRATE_192;
            break;
        default:
            rate = SL_SAMPLINGRATE_44_1;
    }
    return rate;
}



int YsAudioPlayer::resampleAudio() {
    data_size = 0;//每次拿到数据重采样 清空size  在解析音频时这个方法可能每秒回调很多次
    while (!playCost->exit) {
        if(playCost->pause){
            pthread_mutex_lock(&pauseMutex);
            pthread_cond_wait(&pauseCond, &pauseMutex);
            pthread_mutex_unlock(&pauseMutex);
        }
        if(playCost->seek){
            av_usleep(1000 * 100);
            continue;
        }
        if(queue->getQueueSize()==0){
            av_usleep(1000 * 100);
            continue;
        }
        AVPacket *avPacket = av_packet_alloc();
        queue->getAvPacket(avPacket);
        pthread_mutex_lock(&codecMutex);
        int ret = avcodec_send_packet(avCodecContext, avPacket);
        if (ret != 0) {
            av_packet_free(&avPacket);
            pthread_mutex_unlock(&codecMutex);
            continue;
        }
        AVFrame *avFrame = av_frame_alloc();
        ret = avcodec_receive_frame(avCodecContext, avFrame);
        if (ret == 0) {
            // 确定输入声道数
            int inChannels;
            if (avFrame->ch_layout.nb_channels > 0) {
                inChannels = avFrame->ch_layout.nb_channels;
            } else if (avCodecContext->ch_layout.nb_channels > 0) {
                inChannels = avCodecContext->ch_layout.nb_channels;
            } else {
                inChannels = 2;
            }

            AVSampleFormat inFmt = (AVSampleFormat) avFrame->format;
            int inRate = avFrame->sample_rate;

            // ============================================================
            // 缓存 SwrContext：仅当音频格式变化时才重建
            // 修复：原代码每帧都 swr_alloc_set_opts2 + swr_free
            // ============================================================
            if (swrCtx == nullptr ||
                cachedInFmt != inFmt ||
                cachedInRate != inRate ||
                cachedInChannels != inChannels) {

                if (swrCtx) {
                    swr_free(&swrCtx);
                    swrCtx = nullptr;
                }

                AVChannelLayout out_ch_layout = AV_CHANNEL_LAYOUT_STEREO;
                AVChannelLayout in_ch_layout;

                if (avFrame->ch_layout.nb_channels > 0) {
                    av_channel_layout_copy(&in_ch_layout, &avFrame->ch_layout);
                } else if (avCodecContext->ch_layout.nb_channels > 0) {
                    av_channel_layout_copy(&in_ch_layout, &avCodecContext->ch_layout);
                } else {
                    av_channel_layout_default(&in_ch_layout, 2);
                }

                int swr_ret = swr_alloc_set_opts2(
                        &swrCtx,
                        &out_ch_layout,
                        AV_SAMPLE_FMT_S16,
                        inRate,
                        &in_ch_layout,
                        inFmt,
                        inRate,
                        0, nullptr
                );

                av_channel_layout_uninit(&in_ch_layout);

                if (swr_ret < 0 || !swrCtx || swr_init(swrCtx) < 0) {
                    LOGD("swr_alloc_set_opts2 失败");
                    swr_free(&swrCtx);
                    swrCtx = nullptr;
                    av_packet_free(&avPacket);
                    av_frame_free(&avFrame);
                    pthread_mutex_unlock(&codecMutex);
                    continue;
                }

                cachedInFmt = inFmt;
                cachedInRate = inRate;
                cachedInChannels = inChannels;
            }

            int nb = swr_convert(
                    swrCtx,
                    &buffer,
                    avFrame->nb_samples,
                    (const uint8_t **) avFrame->data,
                    avFrame->nb_samples);

            int out_channels = 2; // AV_CH_LAYOUT_STEREO = 2 声道
            data_size = nb * out_channels * av_get_bytes_per_sample(AV_SAMPLE_FMT_S16);

            currentTime = av_q2d(time_base) * avFrame->pts;
            clock = currentTime;

            av_packet_free(&avPacket);
            av_frame_free(&avFrame);
            pthread_mutex_unlock(&codecMutex);
            break;
        } else {
            av_packet_free(&avPacket);
            av_frame_free(&avFrame);
            pthread_mutex_unlock(&codecMutex);
            continue;
        }
    }
    return data_size;
}

void YsAudioPlayer::pause() {
    if (pcmPlayerPlay != NULL) {
        (*pcmPlayerPlay)->SetPlayState(pcmPlayerPlay, SL_PLAYSTATE_PAUSED);;
    }
}

void YsAudioPlayer::resume() {
    if (pcmPlayerPlay != NULL) {
        (*pcmPlayerPlay)->SetPlayState(pcmPlayerPlay, SL_PLAYSTATE_PLAYING);;
    }
    pthread_mutex_lock(&pauseMutex);
    pthread_cond_signal(&pauseCond);
    pthread_mutex_unlock(&pauseMutex);

}

void YsAudioPlayer::release() {
    pause();
    if (queue){
        queue->notifyQueue();
    }
    // 停止播放线程
    if (thread_play) {
        playCost->exit = true;
        pthread_join(thread_play, nullptr);
        thread_play = 0;
    }
    // 释放OpenSL ES相关资源
    if (pcmPlayerObj) {
        (*pcmPlayerObj)->Destroy(pcmPlayerObj);
        pcmPlayerObj = nullptr;
        pcmPlayerPlay = nullptr;
        pcmBufferQueue = nullptr;
    }
    if (outputMixObj) {
        (*outputMixObj)->Destroy(outputMixObj);
        outputMixObj = nullptr;
        outputMixEnvironmentalReverb = nullptr;
    }
    if (engineObj) {
        (*engineObj)->Destroy(engineObj);
        engineObj = nullptr;
        engineEng = nullptr;
    }
    // 释放音频缓冲区
    if (buffer) {
        av_free(buffer);
        buffer = nullptr;
    }
    // 释放缓存的重采样上下文
    if (swrCtx) {
        swr_free(&swrCtx);
        swrCtx = nullptr;
    }
    // 释放解码器上下文
    if (avCodecContext) {
        // FFmpeg 5.x: avcodec_close 已移除，avcodec_free_context 会自动关闭
        avcodec_free_context(&avCodecContext);
        avCodecContext = nullptr;
    }
    // 释放队列
    if (queue) {
        delete queue;
        queue = nullptr;
    }
}
