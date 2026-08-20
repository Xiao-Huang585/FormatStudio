//
// Created by Ding on 2025/6/10.
//

#ifndef YSPLAYER_YSCALLJAVA_H
#define YSPLAYER_YSCALLJAVA_H
#include "jni.h"
#include "AndroidLog.h"
class YsCallJava {
public:
    JavaVM  *javaVm = nullptr;
    JNIEnv  *jniEnv = nullptr;
    jobject  jobj = nullptr;
    jmethodID  jmidPrepare = nullptr;
    jmethodID  jmidCallYuv = nullptr;
    jmethodID  jmidCallNV12 = nullptr;
    jmethodID jmidCallTimeProgress = nullptr;
    jmethodID jmidCallRGB24 = nullptr;

public:
    YsCallJava(JavaVM * jvm,JNIEnv *jniEnv,jobject * jobj);
    ~YsCallJava();
    void callBackYuvData(int width,int height,uint8_t * y ,uint8_t *  u,uint8_t* v);
    void callBackNV12Data(int width,int height,uint8_t * y, uint8_t *uv );
    void callPrepare();

    void onCallTimeInfo(double currentPos, long duration);

    void callBackRGB24Data(int width, int height, uint8_t *rgb);
};


#endif //YSPLAYER_YSCALLJAVA_H
