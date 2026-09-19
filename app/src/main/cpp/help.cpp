#include <jni.h>
#include <filesystem>
#include <fstream>
#include <fstream>
#include <vector>
#include <string>

#include "functions.h"

extern "C" JNIEXPORT jboolean JNICALL
Java_com_kgmdecoder_app_Help_checkEnableExperimentalFunction(JNIEnv *env, jobject thiz) {
    constexpr char* PATH = "/data/user/0/com.kgmdecoder.app/files/config.dat";
    if (!std::filesystem::exists(PATH)) {
        std::ofstream f(PATH);
        f << "0" << std::endl;
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