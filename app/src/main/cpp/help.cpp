#include <jni.h>
#include <filesystem>
#include <fstream>
#include <fstream>
#include <vector>
#include <string>

#include "functions.h"
extern "C" {
#include "libavutil/cpu.h"
}

constexpr char* PATH = "/data/user/0/com.kgmdecoder.app/files/config.dat";

bool checkFile(const char* path_ = PATH) {
    if (!std::filesystem::exists(PATH)) {
        std::ofstream f(PATH);
        f << "0" << std::endl;
        f << "2" << std::endl;
        return false;
    }
    return true;
}

extern "C" JNIEXPORT jboolean JNICALL
Java_com_kgmdecoder_app_Help_checkEnableExperimentalFunction(JNIEnv *env, jobject thiz) {

    if (!checkFile()) {
        return false;
    }

    std::fstream f(PATH);
    std::string line;
    std::getline(f, line);
    if (line == "1") {
        return true;
    } else {
        writeAtLine(PATH, 1, "0");
        return false;
    }
}

extern "C" JNIEXPORT jint JNICALL
Java_com_kgmdecoder_app_Help_getEncodeThreads(JNIEnv *env, jobject thiz) {
    if (!checkFile()) {
        return -1;
    }
    std::fstream f(PATH);
    std::string line;
    std::getline(f, line);
    std::getline(f, line);
    if (line.empty()) return 2;
    std::string target = "";
    for (size_t i = 0; i < line.size(); i++) {
        if ('0' <= line[i] && line[i] <= '9') target.push_back(line[i]);
        else return 2;
    }
    if (target.empty()) return 2;
    int c = std::stoi(target);
    if (c < 1) return 2;
    return c <= av_cpu_count() ? c : av_cpu_count();
}

extern "C" JNIEXPORT jboolean JNICALL
Java_com_kgmdecoder_app_Help_writeLine(JNIEnv *env, jobject thiz,
                                       jstring path_, jint lineIndex, jstring text_) {
    if (!path_ || !text_) return JNI_FALSE;
    const char* path = env->GetStringUTFChars(path_, nullptr);
    const char* text = env->GetStringUTFChars(text_, nullptr);
    bool flag = writeAtLine(path, lineIndex, text);
    env->ReleaseStringUTFChars(path_, path);
    env->ReleaseStringUTFChars(text_, text);
    return flag ? JNI_TRUE : JNI_FALSE;
}