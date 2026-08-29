//
// Created by Ding on 2025/6/6.
//

// 被我用豆包改过了😄

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
    LOGD("decode url %s",url);
    int openRel = avformat_open_input(&avFormatContext, url, NULL,NULL);
    if(openRel!=0){
        int i = AVERROR(openRel);
        LOGD("avformat_open_input fail %d ",i);
        LOGD("avformat_open_input fail %d (%s)", i, av_err2str(openRel));
        // 没有callError接口，只打印日志，注释掉回调
        // if(callJava != nullptr)
        // {
        //     callJava->callError(openRel,"avformat_open_input failed");
        // }
        return;
    }
    int openStreamRel = avformat_find_stream_info(avFormatContext, NULL);
    if(openStreamRel<0){
        LOGD("avformat_find_stream_info fail");
        // if(callJava != nullptr)
        // {
        //     callJava->callError(openStreamRel,"find stream info failed");
        // }
        return;
    }
    int audioStreamIndex = av_find_best_stream(avFormatContext, AVMEDIA_TYPE_AUDIO, -1, -1, NULL, 0);
    if(audioStreamIndex<0){
        LOGD("av_find_best_stream audio fail");
        // if(callJava != nullptr)
        // {
        //     callJava->callError(-1,"no audio stream");
        // }
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
        LOGD("avcodec_parameters_to_context audio fail");
        // callJava->callError(-2,"audio parameters to context fail");
        return;
    }
    if(avcodec_open2(audioCodecContext,pCodec,NULL)!=0){
        LOGD("avcodec_open2 audio fail");
        // callJava->callError(-3,"avcodec_open2 audio fail");
        return;
    }
    ysAudioPlayer->avCodecContext = audioCodecContext;
    ysAudioPlayer->streamIndex  = audioStreamIndex;
    ysAudioPlayer->codecParameters = audioCodecParameters;
    ysAudioPlayer->sample_rate = audioCodecParameters->sample_rate;
    ysAudioPlayer->time_base = pAStream->time_base;
    ysAudioPlayer->duration = avFormatContext->duration / AV_TIME_BASE;
    //===== 视频部分：只有找到视频流才执行初始化，找不到直接跳过，不return！=====
    int videoStreamIndex = av_find_best_stream(avFormatContext, AVMEDIA_TYPE_VIDEO, -1, -1, NULL, 0);
    if(videoStreamIndex >= 0)
    {
        if(ysVideoPlayer == nullptr){
            ysVideoPlayer = new YsVideoPlayer(ysPlayerConst, callJava,ysAudioPlayer);
        }
        AVStream *pVStream = avFormatContext->streams[videoStreamIndex];
        AVCodecParameters *videoParameters = pVStream->codecpar;
        AVCodecID videoCodecId = videoParameters->codec_id;
        int num = pVStream->avg_frame_rate.num;
        int den = pVStream->avg_frame_rate.den;
        int fps = num/den;
        ysVideoPlayer->defauleDelayTime = 1.0 / fps;
        ysVideoPlayer->streamIndex  = videoStreamIndex;
        ysVideoPlayer->codecParameters = videoParameters;
        ysVideoPlayer->time_base = pVStream->time_base;
        if(enalbeMediaCodec){
            createHwDecode();
        }
        if(ysVideoPlayer->avCodecContext == nullptr || ysVideoPlayer->avCodecContext->codec == nullptr){
            if(enalbeMediaCodec){
                LOGD("硬件解码初始化失败，回退到软解码");
            }
            const AVCodec *pVCodec = avcodec_find_decoder(videoCodecId);
            AVCodecContext *videoCodecContext = avcodec_alloc_context3(pVCodec);
            if(avcodec_parameters_to_context(videoCodecContext,videoParameters)>=0)
            {
                videoCodecContext->thread_count = 4;
                videoCodecContext->thread_type = FF_THREAD_FRAME;
                if(avcodec_open2(videoCodecContext,pVCodec,0)==0){
                    ysVideoPlayer->avCodecContext = videoCodecContext;
                }else{
                    LOGD("avcodec_open2 video fail");
                }
            }else{
                LOGD("avcodec_parameters_to_context video fail");
            }
        }
    }
    else
    {
        //纯音频文件，无视频流
        LOGD("av_find_best_stream video fail , this is audio‑only file");
    }
    //无论有没有视频，都走到prepare成功回调
    LOGD("prepare Success");
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
    if (ysVideoPlayer != nullptr)
    {
        ysVideoPlayer->start();
    }
    if(ysAudioPlayer != nullptr)
    {
        ysAudioPlayer->start();
    }
    while(!ysPlayerConst->exit)
    {
        // ⚠️关键修复：判断ysAudioPlayer和内部queue指针不为空
        if (ysAudioPlayer == nullptr || ysAudioPlayer->queue == nullptr)
        {
            av_usleep(1000*5);
            continue;
        }

        if (ysAudioPlayer->queue->getQueueSize() > 120) {
            av_usleep(1000*10);
            continue;
        }
        AVPacket *pPacket = av_packet_alloc();
        if(av_read_frame(avFormatContext,pPacket) == 0){
            if(ysVideoPlayer != nullptr && pPacket->stream_index == ysVideoPlayer->streamIndex){
                ysVideoPlayer->queue->putAvPacket(pPacket);
            }else if(pPacket->stream_index == ysAudioPlayer->streamIndex){
                ysAudioPlayer->queue->putAvPacket(pPacket);
            }else{
                av_packet_unref(pPacket);
            }
        }else{
            break;
        }
        av_packet_free(&pPacket);
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
                            break;
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
            default:
                break;
        }
        ysVideoPlayer->avCodecContext = avcodec_alloc_context3(avCodec);
        //设置编码器上下文avctx的get_format为get_hw_format
        if(avCodec){
            ysVideoPlayer->avCodecContext->get_format = get_hw_format;
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
double YsFFmpegPlayer::now() {
    if(ysAudioPlayer != nullptr)
    {
        return ysAudioPlayer->clock;
    }
    return 0.0;
}
void YsFFmpegPlayer::pause() {
    ysPlayerConst->pause = true;
    if(ysAudioPlayer != nullptr)
    {
        ysAudioPlayer->pause();
    }
}
void YsFFmpegPlayer::resume() {
    ysPlayerConst->pause = false;
    if(ysAudioPlayer != nullptr)
    {
        ysAudioPlayer->resume();
    }
    if(ysVideoPlayer != nullptr)
    {
        ysVideoPlayer->resume();
    }
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
    if(ysAudioPlayer == nullptr)
    {
        return;
    }
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
