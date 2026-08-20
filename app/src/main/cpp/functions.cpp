#include "functions.h"
#include "avcpp/av.h"
#include "avcpp/avlog.h"
extern "C" {
#include <libavutil/log.h>
}

// ====== 全局变量定义 ======
JavaVM *g_javaVM = nullptr;
jclass g_MainActivityClass = nullptr;
jmethodID g_showTextMethodID = nullptr;
jmethodID g_callJavaClearMethodID = nullptr;
jmethodID g_getJavaInputMethodID = nullptr;
jmethodID g_callJavaExitMethodID = nullptr;
jmethodID g_showVideoViewMethodID = nullptr;
jmethodID g_callJavaIsSurfaceClickedMethodID = nullptr;
jmethodID g_callJavaControlPlayArrowVisibilityMethodID = nullptr;
jmethodID g_callJavaSetETHintTextMethodID = nullptr;
std::mutex g_inputMutex;
std::condition_variable g_inputCv;
std::string g_inputData;
std::atomic<bool> g_inputReady = false;
std::string g_pendingFunction;
std::fstream g_log;

// ====== Surface 全局变量 ======
ANativeWindow *g_nativeWindow = nullptr;
std::mutex g_windowMutex;

// ====== KuGou 枚举转字符串 ======
namespace std {
    string to_string(KuGou _k) {
        switch (_k) {
            case KuGou::Ok:
                return "完成!";
            case KuGou::InputFileOpenFailed:
                return "打开输入失败";
            case KuGou::OutputFileOpenFailed:
                return "打开输出失败";
            case KuGou::DecodeFailed:
                return "解码失败";
            case KuGou::InvalidPath:
                return "无效的路径";
            case KuGou::OutputExists:
                return "输出已经存在";
            case KuGou::Unknown:
                return "未知";
            default:
                return "错误";
        }
    }
}

// ====== 时分秒毫秒转换 ======
std::array<int, 4> secondsToMicroseconds(int64_t microsecond) {
    const int HOUR = 3600000;
    const int MINUTE = 60000;
    const int SECOND = 1000;
    int hours = microsecond / HOUR;
    int rem = microsecond % HOUR;
    int minutes = rem / MINUTE;
    int rem1 = rem % MINUTE;
    int seconds = rem1 / SECOND;
    int milliseconds = rem1 % SECOND;
    return {hours, minutes, seconds, milliseconds};
}

// ====== 指针检查工具 ======
template <typename T, typename... Args>
bool checkAllPtrs(T *ptr, Args... args) {
    if (ptr == nullptr) {
        return false;
    }
    if (reinterpret_cast<uintptr_t>(ptr) < 0x1000) {
        return false;
    }
    return checkAllPtrs(args...);
}
inline bool checkAllPtrs() { return true; }

// ====== JNI 全局引用初始化 ======
bool initGlobalRefs(JNIEnv *env) {
    jclass clazz = env->FindClass("com/kgmdecoder/app/MainActivity");
    if (!clazz)
        return false;
    g_MainActivityClass = (jclass)env->NewGlobalRef(clazz);
    env->DeleteLocalRef(clazz);
    g_showTextMethodID = env->GetStaticMethodID(g_MainActivityClass, "showText", "(Ljava/lang/String;)V");
    g_callJavaClearMethodID = env->GetStaticMethodID(g_MainActivityClass, "callJavaClear", "()V");
    g_getJavaInputMethodID = env->GetStaticMethodID(g_MainActivityClass, "getJavaInput", "()V");
    g_callJavaExitMethodID = env->GetStaticMethodID(g_MainActivityClass, "callJavaExit", "()V");
    g_showVideoViewMethodID = env->GetStaticMethodID(g_MainActivityClass, "showVideoView", "(Z)V");
    g_callJavaIsSurfaceClickedMethodID = env->GetStaticMethodID(g_MainActivityClass, "callJavaIsSurfaceClicked", "()Z");
    g_callJavaControlPlayArrowVisibilityMethodID = env->GetStaticMethodID(g_MainActivityClass, "callJavaControlPlayArrowVisibility", "(Z)V");
    g_callJavaSetETHintTextMethodID = env->GetStaticMethodID(g_MainActivityClass, "callJavaSetETHintText", "(Ljava/lang/String;)V");
    return true;
}
void releaseGlobalRefs(JNIEnv *env) {
    if (g_MainActivityClass) {
        env->DeleteGlobalRef(g_MainActivityClass);
        g_MainActivityClass = nullptr;
    }
}
JNIEnv *getThreadJNIEnv() {
    JNIEnv *env = nullptr;
    g_javaVM->AttachCurrentThread(&env, nullptr);
    return env;
}

// ===================== AndroidOutBuf 实现 =====================
AndroidOutBuf::AndroidOutBuf(JNIEnv* env) : m_env(env)
{
    setp(m_buf, m_buf + BUF_SIZE);
}
int AndroidOutBuf::overflow(int c)
{
    // 先把已缓冲的内容刷出，腾出空间
    flush();
    if (c != EOF)
    {
        m_buf[0] = static_cast<char>(c);
        setp(m_buf + 1, m_buf + BUF_SIZE);
    }
    else
    {
        setp(m_buf, m_buf + BUF_SIZE);
    }
    return 0;
}
int AndroidOutBuf::sync()
{
    flush();
    return 0;
}
void AndroidOutBuf::flush()
{
    if (pbase() != pptr())
    {
        m_buffer.append(pbase(), pptr() - pbase());
        setp(m_buf, m_buf + BUF_SIZE);
    }
    if (!m_buffer.empty())
    {
        callJavaShowText(m_env, m_buffer.c_str());
        m_buffer.clear();
    }
}

// ===================== androidOutStream 实现 =====================
androidOutStream::androidOutStream(JNIEnv* env)
        : std::ostream(&m_buf), m_buf(env)
{}
void androidOutStream::format(const char* fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    char buf[1024];
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    (*this) << std::string(buf);
    flush();
}
void androidOutStream::flush()
{
    std::ostream::flush();
}

// ===================== AndroidInBuf 实现 =====================
AndroidInBuf::AndroidInBuf(JNIEnv* env) : m_env(env) {}
int AndroidInBuf::underflow()
{
    if (!m_inputCache.empty())
    {
        size_t copyLen = std::min(m_inputCache.size(), (size_t)511);
        memcpy(m_buf, m_inputCache.data(), copyLen);
        m_buf[copyLen] = '\0';
        m_inputCache.erase(0, copyLen);
        setg(m_buf, m_buf, m_buf + copyLen);
        return static_cast<unsigned char>(*gptr());
    }
    m_inputCache = androidInStream::blockReadInput(m_env);
    if (m_inputCache.empty())
        return EOF;
    // 关键：在输入末尾追加换行符
    // std::istream::operator>>(string&) 读到非空白字符后会持续读取，
    // 直到遇到空白字符或 EOF。如果不加换行，读完整个输入后 operator>>
    // 会再次调用 underflow() → blockReadInput() 阻塞等待新输入，导致死锁
    m_inputCache += '\n';
    return underflow();
}

// ===================== androidInStream 实现 =====================
androidInStream::androidInStream(JNIEnv* env)
        : std::istream(&m_buf), m_buf(env)
{}
std::string androidInStream::blockReadInput(JNIEnv* env)
{
    std::unique_lock<std::mutex> lock(g_inputMutex);
    g_inputReady = false;
    env->CallStaticVoidMethod(g_MainActivityClass, g_getJavaInputMethodID);
    g_inputCv.wait(lock, [] { return g_inputReady.load(); });
    return g_inputData;
}

// ============================
// Java 回调打印文本
// ============================
void callJavaShowText(JNIEnv *env, const char *text) {
    if (!env || !text) return;
    jstring jstr = env->NewStringUTF(text);
    env->CallStaticVoidMethod(g_MainActivityClass, g_showTextMethodID, jstr);
    env->DeleteLocalRef(jstr);
}

// ============================
// 业务回调函数实现（修复g_callExitMethodID笔误）
// ============================
void Jexit() {
    JNIEnv *env = getThreadJNIEnv();
    env->CallStaticVoidMethod(g_MainActivityClass, g_callJavaExitMethodID);
    g_javaVM->DetachCurrentThread();
    pthread_exit(nullptr);
}
void callJavaClear() {
    JNIEnv *env = getThreadJNIEnv();
    if (env == nullptr) {
        LOGD("callJavaClear: 获取JNIEnv失败");
        return;
    }
    if (g_MainActivityClass == nullptr || g_callJavaClearMethodID == nullptr) {
        LOGD("callJavaClear: MainActivity类或callJavaClear方法ID未初始化");
        return;
    }
    env->CallStaticVoidMethod(g_MainActivityClass, g_callJavaClearMethodID);
    if (env->ExceptionCheck()) {
        LOGD("callJavaClear: 调用Java方法发生异常");
        env->ExceptionClear();
    }
    LOGD("控制台已清空");
}
void callJavaShowVideoView(bool show) {
    JNIEnv *env = getThreadJNIEnv();
    if (env == nullptr || g_MainActivityClass == nullptr || g_showVideoViewMethodID == nullptr) {
        LOGD("callJavaShowVideoView: 未初始化");
        return;
    }
    env->CallStaticVoidMethod(g_MainActivityClass, g_showVideoViewMethodID, show ? JNI_TRUE : JNI_FALSE);
    if (env->ExceptionCheck()) {
        LOGD("callJavaShowVideoView: 调用Java方法发生异常");
        env->ExceptionClear();
    }
    LOGI("callJavaShowVideoView(%s)", show ? "true" : "false");
}
void callJavaControlPlayArrowVisibility(bool visibility) {
    JNIEnv *env = getThreadJNIEnv();
    if (!env) {
        LOGD("callJavaControlPlayArrowVisibility: JNIEnv 为空");
        return;
    }
    if (!g_MainActivityClass || !g_callJavaControlPlayArrowVisibilityMethodID) {
        LOGD("callJavaControlPlayArrowVisibility: 类/方法ID未初始化");
        return;
    }
    env->CallStaticVoidMethod(g_MainActivityClass, g_callJavaControlPlayArrowVisibilityMethodID, visibility ? JNI_TRUE : JNI_FALSE);
    if (env->ExceptionCheck()) {
        LOGE("callJavaControlPlayArrowVisibility 调用Java异常");
        env->ExceptionDescribe();
        env->ExceptionClear();
    }
}
bool callJavaIsSurfaceClicked() {
    JNIEnv *env = getThreadJNIEnv();
    if (env == nullptr) {
        LOGD("callJavaIsSurfaceClicked: 获取JNIEnv失败");
        return false;
    }
    if (!g_MainActivityClass || !g_callJavaIsSurfaceClickedMethodID) {
        LOGD("callJavaIsSurfaceClicked: 未初始化");
        return false;
    }
    jboolean res = env->CallStaticBooleanMethod(g_MainActivityClass, g_callJavaIsSurfaceClickedMethodID);
    if (env->ExceptionCheck()) {
        LOGD("callJavaIsSurfaceClick: 调用异常");
        env->ExceptionClear();
        return false;
    }
    return res == JNI_TRUE;
}
void callJavaSetETHintText(const char* text) {
    JNIEnv *env = getThreadJNIEnv();
    if (env == nullptr) {
        LOGD("callJavaSetETHintText: 获取JNIEnv失败");
        return;
    }
    if (!g_MainActivityClass || !g_callJavaSetETHintTextMethodID) {
        LOGD("callJavaSetETHintText: 未初始化");
        return;
    }
    jstring jstr = env->NewStringUTF(text);
    env->CallStaticVoidMethod(g_MainActivityClass, g_callJavaSetETHintTextMethodID, jstr);
    env->DeleteLocalRef(jstr);
    if (env->ExceptionCheck()) {
        LOGD("callJavaSetETHintText: 调用异常");
        env->ExceptionClear();
        return;
    }
}

// ============================
// Surface 窗口管理函数
// ============================
void setNativeWindow(ANativeWindow *window) {
    std::lock_guard<std::mutex> lock(g_windowMutex);
    if (g_nativeWindow) {
        ANativeWindow_release(g_nativeWindow);
        g_nativeWindow = nullptr;
    }
    if (window) {
        ANativeWindow_acquire(window);
        g_nativeWindow = window;
    }
    LOGI("setNativeWindow: %p", g_nativeWindow);
}
ANativeWindow *getNativeWindow() {
    std::lock_guard<std::mutex> lock(g_windowMutex);
    return g_nativeWindow;
}
ANativeWindow *acquireNativeWindow() {
    std::lock_guard<std::mutex> lock(g_windowMutex);
    if (g_nativeWindow) {
        ANativeWindow_acquire(g_nativeWindow);
    }
    return g_nativeWindow;
}
void releaseAcquiredNativeWindow(ANativeWindow *window) {
    if (window) {
        ANativeWindow_release(window);
    }
}
void releaseNativeWindow() {
    std::lock_guard<std::mutex> lock(g_windowMutex);
    if (g_nativeWindow) {
        ANativeWindow_release(g_nativeWindow);
        g_nativeWindow = nullptr;
        LOGI("releaseNativeWindow: 已释放");
    }
}

// ============================
// JNI 加载入口
// ============================
jint JNI_OnLoad(JavaVM *vm, void *reserved) {
    g_javaVM = vm;
    JNIEnv *env;
    vm->GetEnv((void **)&env, JNI_VERSION_1_6);
    initGlobalRefs(env);
    av::init();
    av_log_set_level(AV_LOG_WARNING);
    return JNI_VERSION_1_6;
}

// ============================
// JNI Java传递输入给C++
// ============================
extern "C" JNIEXPORT void JNICALL
Java_com_kgmdecoder_app_MainActivity_passInputToCpp(JNIEnv *env, jclass clazz, jstring jInput) {
    std::lock_guard<std::mutex> lock(g_inputMutex);
    const char *c = env->GetStringUTFChars(jInput, nullptr);
    std::string raw = c;
    env->ReleaseStringUTFChars(jInput, c);

    // 拆分 "路径\n功能名" 格式
    size_t newlinePos = raw.find('\n');
    if (newlinePos != std::string::npos) {
        g_inputData = raw.substr(0, newlinePos);
        g_pendingFunction = raw.substr(newlinePos + 1);
    } else {
        g_inputData = raw;
        g_pendingFunction.clear();
    }
    g_inputReady = true;
    g_inputCv.notify_one();
}

// ============================
// JNI 启动C++主线程
// ============================
extern "C" JNIEXPORT void JNICALL
Java_com_kgmdecoder_app_MainActivity_triggerCppToCallJava(JNIEnv *env, jobject thiz) {
    jobject g_thiz = env->NewGlobalRef(thiz);
    std::thread t([g_thiz]() {
        cppMain(g_thiz);
        JNIEnv* threadEnv = getThreadJNIEnv();
        threadEnv->DeleteGlobalRef(g_thiz);
        g_javaVM->DetachCurrentThread();
    });
    t.detach();
}

// ============================
// Surface JNI 绑定窗口（声明与实现参数完全匹配，修复冲突）
// ============================
extern "C" JNIEXPORT void JNICALL
Java_com_kgmdecoder_app_MainActivity_nativeSetSurface(JNIEnv *env, jobject thiz, jobject surface) {
    if (surface) {
        ANativeWindow *window = ANativeWindow_fromSurface(env, surface);
        setNativeWindow(window);
        LOGI("Surface 已设置: %p", window);
    } else {
        releaseNativeWindow();
        LOGI("Surface 已清除");
    }
}
extern "C" JNIEXPORT void JNICALL
Java_com_kgmdecoder_app_MainActivity_nativeShowVideoView(JNIEnv *env, jobject thiz, jboolean show) {
    LOGI("nativeShowVideoView: %s", show ? "显示" : "隐藏");
}