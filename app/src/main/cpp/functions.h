#pragma once
// ====== Android 开发包 ======
#include <android/log.h>
#include <android/native_window.h>
#include <android/native_window_jni.h>
#include <jni.h>
#include <pthread.h>
// ====== C++ 标准头 ======
#include <chrono>
#include <array>
#include <atomic>
#include <condition_variable>
#include <cstdarg>
#include <cstdlib>
#include <fstream>
#include <mutex>
#include <string>
#include <thread>
#include <sstream>
#include <iostream>
#include <streambuf>
// ====== 配置宏 ======
#define ECHO true
#define LOG_TAG "NativeCallJava"
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)
#define LOGD(...) __android_log_print(ANDROID_LOG_DEBUG, LOG_TAG, __VA_ARGS__)
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)

// ================= 前置声明：解决头文件内调用JNI函数依赖 =================
void callJavaShowText(JNIEnv *env, const char *text);

// ===================== 输出缓冲区 仅声明 =================
class AndroidOutBuf : public std::streambuf
{
private:
    JNIEnv* m_env;
    std::string m_buffer;
    static constexpr size_t BUF_SIZE = 1024;
    char m_buf[BUF_SIZE];
protected:
    int overflow(int c) override;
    int sync() override;
    void flush();
public:
    explicit AndroidOutBuf(JNIEnv* env);
};

// ===================== 输出流 仅声明 =================
class androidOutStream : public std::ostream
{
private:
    AndroidOutBuf m_buf;
public:
    explicit androidOutStream(JNIEnv* env);
    void format(const char* fmt, ...);
    void flush();
};

// ===================== 输入缓冲区 仅声明 =================
class AndroidInBuf : public std::streambuf
{
private:
    JNIEnv* m_env;
    std::string m_inputCache;
    char m_buf[512];
protected:
    int underflow() override;
public:
    explicit AndroidInBuf(JNIEnv* env);
};

// ===================== 输入流 仅声明（修复blockReadInput私有问题） =================
class androidInStream : public std::istream
{
private:
    AndroidInBuf m_buf;
public:
    static std::string blockReadInput(JNIEnv* env);
    explicit androidInStream(JNIEnv* env);
};

// ====== 全局 JavaVM 指针 ======
extern JavaVM *g_javaVM;
extern jclass g_MainActivityClass;
extern jmethodID g_showTextMethodID;
extern jmethodID g_callJavaClearMethodID;
extern jmethodID g_getJavaInputMethodID;
extern jmethodID g_callJavaExitMethodID;
extern jmethodID g_showVideoViewMethodID;
extern jmethodID g_callJavaIsSurfaceClickedMethodID;
extern jmethodID g_callJavaControlPlayArrowVisibilityMethodID;
extern jmethodID g_callJavaSetETHintTextMethodID;
// ====== 线程相关 ======
extern std::mutex g_inputMutex;
extern std::condition_variable g_inputCv;
extern std::string g_inputData;
extern std::atomic<bool> g_inputReady;
extern std::string g_pendingFunction; // Selecting Activity 返回的功能名
// ====== 日志相关 ======
extern std::fstream g_log;
// ====== Surface 相关（YsPlayer 渲染用）======
extern ANativeWindow *g_nativeWindow;
extern std::mutex g_windowMutex;

// 向前声明主函数
void cppMain(jobject thiz);

// ====== Rust KGM 解密函数 ======
extern "C" {
int kgmDecodeFile(const char *inputPath, const char *outputPath);
void kgmFree(void *ptr);
void kgmInit();
}

// KGM 解密状态码
enum class KuGou {
    Ok = 0,
    InputFileOpenFailed = -1,
    OutputFileOpenFailed = -2,
    DecodeFailed = -3,
    InvalidPath = -4,
    OutputExists = -5,
    Unknown = -255
};

namespace std {
    string to_string(KuGou _k);
}

// 时分秒毫秒转换
std::array<int, 4> secondsToMicroseconds(int64_t microsecond);

// 检查空指针工具
inline bool checkAllPtrs();
template <typename T, typename... Args>
bool checkAllPtrs(T *ptr, Args...);

// JNI 全局引用管理
bool initGlobalRefs(JNIEnv *env);
void releaseGlobalRefs(JNIEnv *env);
JNIEnv *getThreadJNIEnv();

// ============================
// Java 回调函数声明
// ============================
void Jexit();
void callJavaClear();
void callJavaShowVideoView(bool show);
bool callJavaIsSurfaceClicked();
void callJavaControlPlayArrowVisibility(bool visibility);
void callJavaSetETHintText(const char *text);

// ============================
// Surface 管理函数
// ============================
void setNativeWindow(ANativeWindow *window);
ANativeWindow *getNativeWindow();
ANativeWindow *acquireNativeWindow();
void releaseAcquiredNativeWindow(ANativeWindow *window);
void releaseNativeWindow();

// ============================
// JNI 原生导出函数（修复nativeShowVideoView参数不匹配）
// ============================
jint JNI_OnLoad(JavaVM *vm, void *reserved);
extern "C" JNIEXPORT void JNICALL
Java_com_kgmdecoder_app_MainActivity_triggerCppToCallJava(JNIEnv *env, jobject thiz);
extern "C" JNIEXPORT void JNICALL
Java_com_kgmdecoder_app_MainActivity_passInputToCpp(JNIEnv *env, jclass clazz, jstring jInput);
extern "C" JNIEXPORT void JNICALL
Java_com_kgmdecoder_app_MainActivity_nativeSetSurface(JNIEnv *env, jobject thiz, jobject surface);
extern "C" JNIEXPORT void JNICALL
Java_com_kgmdecoder_app_MainActivity_nativeShowVideoView(JNIEnv *env, jobject thiz, jboolean show);

// 标准换行兼容
using std::endl;