// ============================================================
// native-lib.cpp
//
// 修改内容：
//   1. 在 getMediaInfo() 之后，如果文件包含视频流
//   2. 询问用户是否播放
//   3. 如果是，通过 YsFFmpegPlayer 播放视频
//
// 测试用，后面要删掉重写
// ============================================================

#include "functions.h"
#include "FFmpeg.h"
#include "YsPlayer/YsFFmpegPlayer.h"
#include "YsPlayer/YsCallJava.h"


// ====== 原生 FFmpeg 头文件（简单播放器用） ======
extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libswscale/swscale.h>
#include <libavutil/imgutils.h>
#include <libavutil/time.h>
}

// 设为 0 用 playVideoSimple（不依赖 YsPlayer）
// 设为 1 用 playVideoWithYsPlayer（依赖 YsPlayer C++ 源码）
#define ENABLE_YSPLAYER true
constexpr const char* defaultHint = "请输入内容，按回车确认 >>";

// ============================================================
// Surface 点击轮询线程
//
// 问题：cin >> cmd 阻塞等待用户输入文字+回车，callJavaIsSurfaceClicked()
//       只在 cin 返回后才被检查，此时 300ms 早已超时。
//
// 解决：开独立线程每 50ms 轮询一次 callJavaIsSurfaceClicked()，
//       检测到点击立即切换暂停/恢复。
// ============================================================
static std::atomic<bool> g_videoPlaying{false};
static YsFFmpegPlayer *g_currentPlayer = nullptr;

static void *surfaceClickPollThread(void *ctx) {
    while (g_videoPlaying.load()) {
        if (callJavaIsSurfaceClicked()) {
            // 检测到点击，切换暂停/恢复
            if (g_currentPlayer && g_currentPlayer->ysPlayerConst) {
                if (g_currentPlayer->ysPlayerConst->pause) {
                    g_currentPlayer->resume();
                    callJavaControlPlayArrowVisibility(false);
                    LOGD("Surface 点击 → 恢复播放");
                } else {
                    g_currentPlayer->pause();
                    callJavaControlPlayArrowVisibility(true);
                    LOGD("Surface 点击 → 暂停播放");
                }
            }
        }
        // 50ms 轮询间隔
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    return nullptr;
}

// ============================================================
// 简单视频播放器（不依赖 YsPlayer，使用原生 FFmpeg + ANativeWindow）
// ============================================================
static void playVideoSimple(androidOutStream &cout, androidInStream &cin,
                            const std::string &path) {
    cout << "===== 开始播放 =====" << endl;
    cout << "文件: " << path << endl;

    AVFormatContext *fmtCtx = avformat_alloc_context();
    if (avformat_open_input(&fmtCtx, path.c_str(), nullptr, nullptr) != 0) {
        cout << "打开文件失败" << endl;
        return;
    }

    if (avformat_find_stream_info(fmtCtx, nullptr) < 0) {
        cout << "获取流信息失败" << endl;
        avformat_close_input(&fmtCtx);
        return;
    }

    int videoStreamIdx = -1;
    for (int i = 0; i < (int)fmtCtx->nb_streams; i++) {
        if (fmtCtx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
            videoStreamIdx = i;
            break;
        }
    }
    if (videoStreamIdx < 0) {
        cout << "未找到视频流" << endl;
        avformat_close_input(&fmtCtx);
        return;
    }

    AVCodecParameters *codecpar = fmtCtx->streams[videoStreamIdx]->codecpar;
    cout << "视频: " << codecpar->width << "x" << codecpar->height << endl;

    const AVCodec *codec = avcodec_find_decoder(codecpar->codec_id);
    if (!codec) {
        cout << "找不到解码器: " << avcodec_get_name(codecpar->codec_id) << endl;
        avformat_close_input(&fmtCtx);
        return;
    }

    AVCodecContext *codecCtx = avcodec_alloc_context3(codec);
    avcodec_parameters_to_context(codecCtx, codecpar);
    codecCtx->thread_count = 2;

    if (avcodec_open2(codecCtx, codec, nullptr) < 0) {
        cout << "打开解码器失败" << endl;
        avcodec_free_context(&codecCtx);
        avformat_close_input(&fmtCtx);
        return;
    }

    // 显示 SurfaceView 并等待 Surface 创建
    callJavaShowVideoView(true);
    ANativeWindow *window = nullptr;
    for (int i = 0; i < 30; i++) {
        window = getNativeWindow();
        if (window) break;
        LOGD("等待 Surface 创建... (%d/30)", i + 1);
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    if (!window) {
        cout << "Surface 创建超时！请重试" << endl;
        callJavaShowVideoView(false);
        avcodec_free_context(&codecCtx);
        avformat_close_input(&fmtCtx);
        return;
    }

    int videoWidth = codecpar->width;
    int videoHeight = codecpar->height;
    ANativeWindow_setBuffersGeometry(window,
                                     videoWidth, videoHeight, WINDOW_FORMAT_RGBA_8888);

    SwsContext *swsCtx = sws_getContext(
            videoWidth, videoHeight, codecCtx->pix_fmt,
            videoWidth, videoHeight, AV_PIX_FMT_RGBA,
            SWS_BILINEAR, nullptr, nullptr, nullptr);

    if (!swsCtx) {
        cout << "初始化格式转换失败" << endl;
        callJavaShowVideoView(false);
        avcodec_free_context(&codecCtx);
        avformat_close_input(&fmtCtx);
        return;
    }

    AVFrame *frame = av_frame_alloc();
    AVFrame *rgbaFrame = av_frame_alloc();
    int rgbaBufSize = av_image_get_buffer_size(
            AV_PIX_FMT_RGBA, videoWidth, videoHeight, 1);
    uint8_t *rgbaBuffer = (uint8_t *)av_malloc(rgbaBufSize);
    av_image_fill_arrays(rgbaFrame->data, rgbaFrame->linesize,
                         rgbaBuffer, AV_PIX_FMT_RGBA, videoWidth, videoHeight, 1);

    AVRational timeBase = fmtCtx->streams[videoStreamIdx]->time_base;
    double frameDuration = 0;
    if (timeBase.den > 0 && timeBase.num > 0) {
        frameDuration = av_q2d(timeBase) * 1000.0;
    }

    cout << "帧率时间基: " << frameDuration << " ms/帧" << endl;
    cout << "播放中... 输入 stop 停止" << endl;
    cout.flush();

    AVPacket *pkt = av_packet_alloc();
    int64_t startTime = av_gettime_relative();
    int frameCount = 0;
    bool playing = true;

    while (playing) {
        {
            std::lock_guard<std::mutex> lock(g_inputMutex);
            if (g_inputReady.load() && g_inputData == "stop") {
                playing = false;
                break;
            }
        }

        int ret = av_read_frame(fmtCtx, pkt);
        if (ret < 0) {
            cout << "播放结束" << endl;
            break;
        }

        if (pkt->stream_index != videoStreamIdx) {
            av_packet_unref(pkt);
            continue;
        }

        ret = avcodec_send_packet(codecCtx, pkt);
        if (ret < 0) {
            av_packet_unref(pkt);
            continue;
        }

        while (ret >= 0) {
            ret = avcodec_receive_frame(codecCtx, frame);
            if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) break;
            if (ret < 0) break;

            sws_scale(swsCtx,
                      frame->data, frame->linesize, 0, videoHeight,
                      rgbaFrame->data, rgbaFrame->linesize);

            ANativeWindow_Buffer windowBuffer;
            if (ANativeWindow_lock(window, &windowBuffer, nullptr) == 0) {
                uint8_t *dst = (uint8_t *)windowBuffer.bits;
                uint8_t *src = rgbaFrame->data[0];
                int dstStride = windowBuffer.stride * 4;
                int srcStride = rgbaFrame->linesize[0];

                for (int y = 0; y < videoHeight; y++) {
                    memcpy(dst + y * dstStride, src + y * srcStride,
                           (size_t)videoWidth * 4);
                }
                ANativeWindow_unlockAndPost(window);
            }

            frameCount++;

            if (frameDuration > 0) {
                int64_t elapsed = av_gettime_relative() - startTime;
                int64_t expected = (int64_t)(frameCount * frameDuration * 1000);
                if (elapsed < expected) {
                    av_usleep(expected - elapsed);
                }
            }
        }
        av_packet_unref(pkt);
    }

    av_packet_free(&pkt);
    sws_freeContext(swsCtx);
    av_free(rgbaBuffer);
    av_frame_free(&rgbaFrame);
    av_frame_free(&frame);
    avcodec_free_context(&codecCtx);
    avformat_close_input(&fmtCtx);

    callJavaShowVideoView(false);

    cout << "共渲染 " << frameCount << " 帧" << endl;
    cout.flush();
}

// ============================================================
// YsPlayer 播放器（匹配 YsFFmpegPlayer 实际 API）
//
// YsPlayer 的工作方式：
//   1. YsCallJava 持有 Java Activity 的 jobject
//   2. YsFFmpegPlayer 需要 YsCallJava* 作为构造参数
//   3. 解码后通过 YsCallJava 回调 Java 方法：
//      - onCallPrepare()     准备完成
//      - onCallYuvData()     YUV420P 数据（软解）
//      - onCallNV12Data()    NV12 数据（硬解）
//      - onCallProgress()    播放进度
//   4. Java 端接收 YUV 数据后用 OpenGL ES 渲染
//   5. 不需要 ANativeWindow，不直接渲染到 Surface
// ============================================================
#if ENABLE_YSPLAYER
static void playVideoWithYsPlayer(androidOutStream &cout, androidInStream &cin,
                                  const std::string &path, jobject activity) {
    cout << "===== YsPlayer 播放 =====" << endl;
    cout << "文件: " << path << endl;

    // 1. 创建 YsCallJava（需要 JavaVM、JNIEnv、Activity jobject）
    //    YsCallJava 的回调方法名对应 Java Activity 中的：
    //    onCallPrepare / onCallYuvData / onCallNV12Data / onCallProgress
    JNIEnv *env = getThreadJNIEnv();
    YsCallJava *callJava = new YsCallJava(g_javaVM, env, &activity);
    if (!callJava) {
        cout << "创建 YsCallJava 失败" << endl;
        return;
    }

    // 2. 创建 YsFFmpegPlayer（构造函数需要 YsCallJava*）
    YsFFmpegPlayer *player = new YsFFmpegPlayer(callJava);
    if (!player) {
        cout << "创建 YsFFmpegPlayer 失败" << endl;
        delete callJava;
        return;
    }

    // 3. 启用硬解
    player->enableMediaCodec(true);

    // 4. 显示 SurfaceView（Java 端的 GLSurfaceView 会接收 YUV 数据渲染）
    callJavaShowVideoView(true);

    // 5. 准备（内部创建解码线程，完成后回调 onCallPrepare）
    cout << "正在准备..." << endl;
    cout.flush();
    player->prepare(path.c_str());

    // 5.5 等待 prepare 完成
    // prepare() 只是启动了一个线程做初始化（创建 ysVideoPlayer/ysAudioPlayer）
    // 必须等它们创建完毕后才能调用 start()，否则空指针崩溃
    cout << "等待初始化完成..." << endl;
    cout.flush();
    for (int i = 0; i < 100; i++) {  // 最多等 10 秒
        if (player->ysVideoPlayer != nullptr && player->ysAudioPlayer != nullptr) {
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    if (player->ysVideoPlayer == nullptr || player->ysAudioPlayer == nullptr) {
        cout << "初始化超时！可能是不支持的视频格式" << endl;
        cout.flush();
        player->release();
        delete player;
        callJavaShowVideoView(false);
        return;
    }

    // 6. 开始播放
    player->start();
    cout << "播放中... 点击视频暂停/恢复，输入 stop 停止" << endl;
    cout.flush();

    // ============================================================
    // 7. 启动 Surface 点击轮询线程
    //    cin >> cmd 会阻塞等待文字输入，无法及时响应点击事件
    //    所以开一个独立线程来轮询 callJavaIsSurfaceClicked()
    // ============================================================
    g_currentPlayer = player;
    g_videoPlaying = true;
    pthread_t clickThread;
    pthread_create(&clickThread, nullptr, surfaceClickPollThread, nullptr);
    // 设置输入框提示
    callJavaSetETHintText("输入\"stop\"退出, 点击屏幕可暂停/继续");

    // 8. 等待用户输入命令
    while (true) {
        std::string cmd;
        cin >> cmd;

        if (cmd == "stop") {
            break;
        } else if (cmd == "pause") {
            player->pause();
            callJavaControlPlayArrowVisibility(true);
            cout << "已暂停" << endl;
        } else if (cmd == "resume") {
            player->resume();
            callJavaControlPlayArrowVisibility(false);
            cout << "已恢复" << endl;
        } else {
            // seek 参数是秒（不是毫秒）
            try {
                int seekSeconds = std::stoi(cmd);
                player->seek(seekSeconds);
                cout << "跳转到: " << seekSeconds << " 秒" << endl;
            } catch (...) {
                cout << "未知命令: " << cmd << endl;
            }
        }
        cout.flush();
    }

    callJavaSetETHintText(defaultHint);
    callJavaControlPlayArrowVisibility(false);
    // 9. 停止点击轮询线程
    g_videoPlaying = false;
    pthread_join(clickThread, nullptr);
    g_currentPlayer = nullptr;

    // 10. 释放（注意是 release() 不是 stop()）
    player->release();
    delete player;

    // YsFFmpegPlayer::release() 内部会 delete callJava
    // 如果 release 已删除 callJava，这里不要重复 delete
    // 安全起见只 delete player，让 player 的析构管理 callJava

    callJavaShowVideoView(false);

    cout << "播放器已释放" << endl;
    cout.flush();
}
#endif

// ============================================================
// 播放视频入口
// ============================================================
static void playVideo(androidOutStream &cout, androidInStream &cin,
                      const std::string &path, jobject activity) {
#if ENABLE_YSPLAYER
    playVideoWithYsPlayer(cout, cin, path, activity);
#else
    playVideoSimple(cout, cin, path);
#endif
}

// ============================================================
// KGM 解密
// ============================================================
static void decryptKGMFile(JNIEnv *env, const std::string &inputPath,
                           androidOutStream &cout, androidInStream &cin,
                           size_t dot) {
    cout << "===== KGM解密模式 =====" << endl;
    std::string outPath;
    outPath = inputPath.substr(0, dot + 1);
    outPath += "mp3";
    cout << "输出" << outPath << endl;

    KuGou code = (KuGou)kgmDecodeFile(inputPath.c_str(), outPath.c_str());
    if ((int)code == 0) {
        cout << "解密成功！输出到: " << outPath << endl;
    } else {
        cout << "解密失败！因为: " << std::to_string(code) << endl;
    }
    cout << "三秒后自动退出..." << endl;
    std::this_thread::sleep_for(std::chrono::milliseconds(3000));
}

// ============================================================
// 主逻辑
// ============================================================
void cppMain(jobject thiz) {
    JNIEnv *env = getThreadJNIEnv();
    androidInStream cin(env);
    androidOutStream cout(env);
    FFmpeg ffmpeg(cout, cin);

    g_log.open("/storage/emulated/0/Android/data/com.kgmdecoder.app/files/log.txt");
    g_log << "C++ I/O is ready..." << std::endl;

    cout << "KGM-Decoder Version Test测试版 小黄牛逼";
    cout.flush();

    while (true) {
        callJavaShowText(env, "---------------------");
        callJavaShowText(env, "支持类型：mp3 wav mp4 avi kgm(kgm.flac)");
        callJavaShowText(env, "请输入输入文件路径:");

        std::string input;
        cin >> input;

        if (input == "exit") {
            Jexit();
            return;
        }
        if (input == "clear") {
            callJavaClear();
            continue;
        }

        // 从全局变量读取功能名（passInputToCpp 已拆分）
        std::string functionName;
        {
            std::lock_guard<std::mutex> lock(g_inputMutex);
            functionName = g_pendingFunction;
            g_pendingFunction.clear();
        }

        std::string fullPath =
                (input[0] == '/') ? input : "/storage/emulated/0/" + input;
        cout << "访问:" << fullPath << endl;
        if (!functionName.empty()) {
            cout << "功能: " << functionName << endl;
        }
        cout.flush();

        std::ifstream test(fullPath);
        if (!test.is_open()) {
            cout << "文件不存在！" << endl;
            cout << "三秒后自动退出..." << endl;
            std::this_thread::sleep_for(std::chrono::milliseconds(3000));
            callJavaClear();
            continue;
        }
        test.close();

        size_t dot = fullPath.find_last_of('.');
        size_t secondDot = fullPath.find_last_of('.', dot - 1);
        std::string ext = dot == std::string::npos ? "" : fullPath.substr(dot);
        std::string secondExt = secondDot == std::string::npos ? "" : fullPath.substr(secondDot);

        if (ext == ".kgm") {
            decryptKGMFile(env, fullPath, cout, cin, dot);
        } else if (secondExt == ".kgm.flac") {
            decryptKGMFile(env, fullPath, cout, cin, secondDot);
            callJavaClear();
        } else {
            int openRet = ffmpeg.openInput(fullPath.c_str());
            LOGD("openInput 返回: %d", openRet);
            if (openRet == 0) {
                ffmpeg.getMediaInfo();
                LOGD("解析完成");

                // 功能名分发
                if (functionName == "GetMediaInfo") {
                    cout << "输入任意键继续..." << endl;
                    cout.flush();
                    std::string dummy;
                    cin >> dummy;
                    callJavaClear();
                    continue;
                }

                if (functionName == "PlayVideo" && ffmpeg.hasVideo()) {
                    ffmpeg.close();
                    callJavaClear();

                    cout << "开始播放..." << endl;
                    cout.flush();
                    playVideo(cout, cin, fullPath, thiz);

                    cout << "\n播放完毕，输入任意键继续..." << endl;
                    cout.flush();
                    std::string dummy;
                    cin >> dummy;
                    callJavaClear();
                    continue;
                }

                // 无功能名 → 手动交互
                if (ffmpeg.hasVideo()) {
                    cout << "\n检测到视频流！" << endl;
                    cout << "输入 play 播放视频，输入其他跳过" << endl;
                    cout.flush();

                    std::string cmd;
                    cin >> cmd;

                    if (cmd == "play") {
                        ffmpeg.close();
                        callJavaClear();

                        cout << "开始播放..." << endl;
                        cout.flush();
                        playVideo(cout, cin, fullPath, thiz);

                        cout << "\n播放完毕，输入任意键继续..." << endl;
                        cout.flush();
                        std::string dummy;
                        cin >> dummy;
                        callJavaClear();
                        continue;
                    }
                }

                cout << "在缓冲区输入任意键回车..." << endl;
                cout.flush();

                std::string dummy;
                cin >> dummy;
                if (dummy == "1") {
                    cout << "输入压缩倍数:";
                    int rate = 0;
                    cin >> rate;
                    if (rate >= 0) {
                        ffmpeg.compressMedia("/sdcard/Download/a.mp4");
                        cin >> dummy;
                        callJavaClear();
                        continue;
                    }
                }
            } else {
                cout << "打开文件失败，错误码: " << openRet << endl;
            }
            callJavaClear();
            continue;
        }
        callJavaClear();
    }
}
