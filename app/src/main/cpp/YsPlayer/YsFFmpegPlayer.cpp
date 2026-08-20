//
// Created by Ding on 2025/6/6.
//

#include "YsFFmpegPlayer.h"

YsFFmpegPlayer::YsFFmpegPlayer(YsCallJava *pJava) {
    ysPlayerConst  = new YsPlayerConst();
    this->callJava = pJava;
    pthread_mutex_init(&seek_mutex, nullptr);
}

YsFFmpegPlayer::~YsFFmpegPlayer() {
    LOGD("析构函数")
    pthread_mutex_destroy(&seek_mutex);
    //TODO
    if (url != nullptr) {
        free((void*)url);
        url = nullptr;
    }
}

void* initDecode(void * ctx){
    YsFFmpegPlayer * ysFFmpegPlayer = static_cast<YsFFmpegPlayer *>(ctx);
    ysFFmpegPlayer->ffmpegDecode();
//    pthread_exit(nullptr)
    return nullptr;
}

void YsFFmpegPlayer::prepare(const char *url) {
    //TODO
    this->url = strdup(url);
    LOGD("thread %ld %s",initDecodeThread,this->url)
    pthread_create(&initDecodeThread,nullptr, initDecode, this);
}
void logCallBack(void *, int level, const char * format, va_list args){
    char log[1024];
    vsnprintf(log, 1024, format, args);
    if(level<=16) {
        LOGS(log)
    }
}
void YsFFmpegPlayer::ffmpegDecode() {
    av_log_set_callback(logCallBack);
    avformat_network_init();
    avFormatContext = avformat_alloc_context();
    /* AVDictionary * option;
     av_dict_set(&option,"key","value",0);*/
//    AVDictionary * options;
//    av_dict_set(&options, "fflags", "discardcorrupt", 0);
//    av_dict_set(&options, "rtsp_transport", "tcp", 0);
    LOGD("decode url %s",url)
    int openRel = avformat_open_input(&avFormatContext, url, NULL,NULL);
    if(openRel!=0){
        //TODO open fail
        int i = AVERROR(openRel);
        LOGD("avformat_open_input fail %d ",i)
        LOGD("avformat_open_input fail %d (%s)", i, av_err2str(openRel));
        return;
    }
    int openStreamRel = avformat_find_stream_info(avFormatContext, NULL);
    if(openStreamRel<0){
        //TODO find stream info  fail
        LOGD("avformat_find_stream_info fail")
        return;
    }
    int audioStreamIndex = av_find_best_stream(avFormatContext, AVMEDIA_TYPE_AUDIO, -1, -1, NULL, 0);
    if(audioStreamIndex<0){
        LOGD("av_find_best_stream audio fail")
        return;
    }
    if(ysAudioPlayer == nullptr){
        ysAudioPlayer = new YsAudioPlayer(ysPlayerConst,callJava);
    }
    AVStream *pAStream = avFormatContext->streams[audioStreamIndex];
    AVCodecParameters *audioCodecParameters = pAStream->codecpar;
    AVCodecID audioCodecId = audioCodecParameters->codec_id;
    const AVCodec *pCodec = avcodec_find_decoder(audioCodecId);
    AVCodecContext *audioCodecContext = avcodec_alloc_context3(pCodec);
    if(avcodec_parameters_to_context(audioCodecContext,audioCodecParameters)<0){
        LOGD("avcodec_parameters_to_context audio fail")
        return;
    }
    if(avcodec_open2(audioCodecContext,pCodec,NULL)!=0){
        LOGD("avcodec_open2 audio fail")
        return;
    }
    ysAudioPlayer->avCodecContext = audioCodecContext;
    ysAudioPlayer->streamIndex  = audioStreamIndex;
    ysAudioPlayer->codecParameters = audioCodecParameters;
    //TODO new
    ysAudioPlayer->sample_rate = audioCodecParameters->sample_rate;
    ysAudioPlayer->time_base = pAStream->time_base;
    ysAudioPlayer->duration = avFormatContext->duration / AV_TIME_BASE; //得到几 秒
    //============================================ 华丽的分割线=======================================
    int videoStreamIndex = av_find_best_stream(avFormatContext, AVMEDIA_TYPE_VIDEO, -1, -1, NULL, 0);
    if(videoStreamIndex<0){
        LOGD("av_find_best_stream video fail ")
        return;
    }
    if(ysVideoPlayer == nullptr){
        ysVideoPlayer = new YsVideoPlayer(ysPlayerConst, callJava,ysAudioPlayer);
    }
    AVStream *pVStream = avFormatContext->streams[videoStreamIndex];
    AVCodecParameters *videoParameters = pVStream->codecpar;
    AVCodecID videoCodecId = videoParameters->codec_id;
    int num = pVStream->avg_frame_rate.num;
    int den = pVStream->avg_frame_rate.den;
    int fps = num/den; // 计算出帧率  1秒播放多少帧   1 / fps
    ysVideoPlayer->defauleDelayTime = 1.0 / fps;
    ysVideoPlayer->streamIndex  = videoStreamIndex; //
    ysVideoPlayer->codecParameters = videoParameters;
    ysVideoPlayer->time_base = pVStream->time_base;
    if(enalbeMediaCodec){
        createHwDecode();
    }
    // 如果硬解码未成功（avCodecContext 为空或未绑定 codec），回退到软解码
    if(ysVideoPlayer->avCodecContext == nullptr ||
       ysVideoPlayer->avCodecContext->codec == nullptr){
        if(enalbeMediaCodec){
            LOGD("硬件解码初始化失败，回退到软解码")
        }
        const AVCodec *pVCodec = avcodec_find_decoder(videoCodecId);
        AVCodecContext *videoCodecContext = avcodec_alloc_context3(pVCodec);
        if(avcodec_parameters_to_context(videoCodecContext,videoParameters)<0){
            LOGD("avcodec_parameters_to_context video fail")
            return;
        }
        // 软解码开启多线程（关键性能优化！单线程解码1080p会非常卡）
        videoCodecContext->thread_count = 4;
        videoCodecContext->thread_type = FF_THREAD_FRAME;
        if(avcodec_open2(videoCodecContext,pVCodec,0)!=0){
            LOGD("avcodec_open2 video fail")
            return;
        }
        ysVideoPlayer->avCodecContext = videoCodecContext;
    }
    LOGD("prepare Success")
    callJava->callPrepare();
}

void *startThread(void *ctx){
    LOGD("startDecode")
    YsFFmpegPlayer * ysFFmpegPlayer = static_cast<YsFFmpegPlayer *>(ctx);
    ysFFmpegPlayer->ffmpegStart();
    return nullptr;
}

void YsFFmpegPlayer::start() {
    pthread_create(&startDecodeThread,nullptr, startThread, this);
}

void YsFFmpegPlayer::ffmpegStart() {
    ysVideoPlayer->start();
    ysAudioPlayer->start();
    while(!ysPlayerConst->exit){
        //优化 硬解码 花屏问题
        //如果队列解码帧过少 ysVideoPlayer 的queue size 限制
        // 音频数据不完整出现了 音频卡顿问题。所以现在无法解决 硬解码花屏问题
        // 如果>5 的范围可以实现硬解 不花屏。但是新闻视频 音频出现卡顿。。
        //如果直接用ysAudioPlayer 的queue size 可以解决音频卡顿。但是硬解花屏还是存在
        //********1080p 这个size 上限 需要扩大 不然近远景切换 画面卡顿
        if (ysAudioPlayer->queue->getQueueSize() > 120) {
            av_usleep(1000*10);
            continue;
        }
        AVPacket *pPacket = av_packet_alloc();
        if(av_read_frame(avFormatContext,pPacket) == 0){ //
            if(pPacket->stream_index == ysVideoPlayer->streamIndex){
                ysVideoPlayer->queue->putAvPacket(pPacket);
            }else if(pPacket->stream_index == ysAudioPlayer->streamIndex){
                //放到 audio 对列中
                ysAudioPlayer->queue->putAvPacket(pPacket);
            }else{
                av_packet_free(&pPacket);
                av_free(pPacket);
            }
        }else{
            //
            break;
        }
    }
}
AVPixelFormat hw_pix_fmt;
static enum AVPixelFormat get_hw_format(AVCodecContext *ctx,
                                        const enum AVPixelFormat *pix_fmts)
{
    const enum AVPixelFormat *p;
    for (p = pix_fmts; *p != -1; p++) {
        if (*p == hw_pix_fmt)
            return *p;
    }
    return AV_PIX_FMT_NONE;
}

void YsFFmpegPlayer::createHwDecode() {
    if(ysVideoPlayer->codecParameters->codec_type==AVMEDIA_TYPE_VIDEO){
        const AVCodec *avCodec = NULL;
        switch (ysVideoPlayer->codecParameters->codec_id) {
            // 这里以h264为例
            case AV_CODEC_ID_H264:
                avCodec = avcodec_find_decoder_by_name("h264_mediacodec");
                if (nullptr == avCodec) {
                    LOGS("没有找到硬解码器h264_mediacodec");
                } else {
                    // 配置硬解码器
                    int i;
                    for (i = 0;; i++) {
                        const AVCodecHWConfig *config = avcodec_get_hw_config(avCodec, i);
                        if (nullptr == config) {
                            LOGS("获取硬解码是配置失败");
                        }
                        if (config->methods & AV_CODEC_HW_CONFIG_METHOD_HW_DEVICE_CTX &&
                            config->device_type == AV_HWDEVICE_TYPE_MEDIACODEC) {
                            hw_pix_fmt = config->pix_fmt;
                            LOGS("硬件解码器配置成功");
                            break;
                        }
                    }
                    break;
                }
        }
        ysVideoPlayer->avCodecContext = avcodec_alloc_context3(avCodec);
        //设置编码器上下文avctx的get_format为get_hw_format
        if(avCodec){
            ysVideoPlayer->avCodecContext->get_format = get_hw_format;
            // 硬件解码器初始化
            AVBufferRef *hw_device_ctx = nullptr;
            avcodec_parameters_to_context(ysVideoPlayer->avCodecContext,ysVideoPlayer->codecParameters);
            int ret = av_hwdevice_ctx_create(&hw_device_ctx, AV_HWDEVICE_TYPE_MEDIACODEC,
                                             nullptr, nullptr, 0);
            if (ret < 0) {
                LOGD("Failed to create specified HW device");
            }else{
                ysVideoPlayer->avCodecContext->codec_id = ysVideoPlayer->codecParameters->codec_id;
                ysVideoPlayer->avCodecContext->codec_type = AVMEDIA_TYPE_VIDEO;
                ysVideoPlayer->avCodecContext->hw_device_ctx = av_buffer_ref(hw_device_ctx);
                if (avcodec_open2(ysVideoPlayer->avCodecContext, avCodec, 0) != 0) {
                    LOGD("open fail");
                }else{
                    LOGD("media codec success ");
                }
            }

        }
    }
}

void YsFFmpegPlayer::pause() {
    ysPlayerConst->pause = true;
    ysAudioPlayer->pause();
}

void YsFFmpegPlayer::resume() {
    ysPlayerConst->pause = false;
    ysAudioPlayer->resume();
    ysVideoPlayer->resume();
}

void YsFFmpegPlayer::release() {
    if(ysPlayerConst){
        ysPlayerConst->exit = true;
    }
    pthread_join(initDecodeThread, nullptr);
    pthread_join(startDecodeThread, nullptr);
    if(ysVideoPlayer){
        ysVideoPlayer->release();
        delete ysVideoPlayer;
        ysVideoPlayer = nullptr;
    }
    if(ysAudioPlayer){
        ysAudioPlayer->release();
        delete ysAudioPlayer;
        ysAudioPlayer = nullptr;
    }
    if(avFormatContext){
        avformat_close_input(&avFormatContext);
        avformat_free_context(avFormatContext);
        avFormatContext = nullptr;
    }
    if(ysPlayerConst){
        delete ysPlayerConst;
        ysPlayerConst = nullptr;
    }
    if(callJava){
        delete callJava;
        callJava = nullptr;
    }
    if(url){
        free((void*)url);
        url = nullptr;
    }
    LOGD("释放 YsFFmpegPlayer 资源完成")
}

void YsFFmpegPlayer::seek(int seconds) {
    int duration  = ysAudioPlayer->duration;
    if(ysAudioPlayer->duration<=0){
        return;
    }
    if (seconds >= 0 && seconds <= duration) {
        ysPlayerConst->seek = true;
        pthread_mutex_lock(&seek_mutex);
        int64_t rel = seconds * AV_TIME_BASE;
        LOGE("rel time %lld", seconds);
        avformat_seek_file(avFormatContext, -1,
                           INT64_MIN, rel, INT64_MAX, 0);
        if (ysAudioPlayer != NULL) {
            ysAudioPlayer->queue->clearQueue();
            ysAudioPlayer->clock = 0;
            ysAudioPlayer->preTime = 0;
            pthread_mutex_lock(&ysAudioPlayer->codecMutex);
            avcodec_flush_buffers(ysAudioPlayer->avCodecContext);
            pthread_mutex_unlock(&ysAudioPlayer->codecMutex);
        }
        if (ysVideoPlayer != NULL) {
            ysVideoPlayer->queue->clearQueue();
            pthread_mutex_lock(&ysVideoPlayer->codecMutex);
            avcodec_flush_buffers(ysVideoPlayer->avCodecContext);
            pthread_mutex_unlock(&ysVideoPlayer->codecMutex);
        }
        pthread_mutex_unlock(&seek_mutex);
        ysPlayerConst->seek = false;
    }


}

void YsFFmpegPlayer::enableMediaCodec(bool b) {
    enalbeMediaCodec = b;
}


