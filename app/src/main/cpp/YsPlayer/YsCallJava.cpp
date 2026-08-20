//
// Created by Ding on 2025/6/10.
//

#include "YsCallJava.h"


YsCallJava::YsCallJava(JavaVM *jvm, JNIEnv *jniEnv, jobject *jobj) {
    this->javaVm = jvm;
    this->jniEnv = jniEnv;
    this->jobj = *jobj;
    this->jobj  = jniEnv->NewGlobalRef(this->jobj);
    jclass pJclass = jniEnv->GetObjectClass(this->jobj);
    jmidPrepare = jniEnv->GetMethodID(pJclass,"onCallPrepare","()V");
    jmidCallYuv = jniEnv->GetMethodID(pJclass,"onCallYuvData","(II[B[B[B)V");
    jmidCallNV12 = jniEnv->GetMethodID(pJclass,"onCallNV12Data","(II[B[B)V");
    jmidCallTimeProgress = jniEnv->GetMethodID(pJclass,"onCallProgress","(DJ)V");
    jmidCallRGB24= jniEnv->GetMethodID(pJclass, "onCallRGB24Data", "(II[B)V");

}

YsCallJava::~YsCallJava() {
    // 释放构造函数中创建的 jobject 全局引用
    if (jobj && javaVm) {
        JNIEnv *env = nullptr;
        bool needDetach = false;
        if (javaVm->GetEnv(reinterpret_cast<void **>(&env), JNI_VERSION_1_4) != JNI_OK) {
            if (javaVm->AttachCurrentThread(&env, nullptr) == JNI_OK) {
                needDetach = true;
            }
        }
        if (env) {
            env->DeleteGlobalRef(jobj);
            jobj = nullptr;
        }
        if (needDetach) {
            javaVm->DetachCurrentThread();
        }
    }
}

void YsCallJava::callPrepare() {
    JNIEnv  * env;
    if(javaVm->AttachCurrentThread(&env, nullptr)!=JNI_OK){
        return;
    };
    env->CallVoidMethod(jobj,jmidPrepare);
    javaVm->DetachCurrentThread();
}

void YsCallJava::callBackYuvData(int width, int height, uint8_t *y, uint8_t * u, uint8_t* v) {
    JNIEnv  * env;
    if(javaVm->AttachCurrentThread(&env, nullptr)!=JNI_OK){
        return;
    };
    jbyteArray  jbyteY = env->NewByteArray(width*height);
    env->SetByteArrayRegion(jbyteY, 0,width*height, reinterpret_cast<const jbyte *>(y));
    jbyteArray  jbyteU = env->NewByteArray(width*height/4);
    env->SetByteArrayRegion(jbyteU, 0,width*height/4, reinterpret_cast<const jbyte *>(u));

    jbyteArray  jbyteV = env->NewByteArray(width*height/4);
    env->SetByteArrayRegion(jbyteV, 0,width*height/4, reinterpret_cast<const jbyte *>(v));

    //TODO
    env->CallVoidMethod(jobj,jmidCallYuv,width,height,jbyteY,jbyteU,jbyteV);
    env->DeleteLocalRef(jbyteY);
    env->DeleteLocalRef(jbyteU);
    env->DeleteLocalRef(jbyteV);

    javaVm->DetachCurrentThread();
}
void YsCallJava::callBackRGB24Data(int width, int height, uint8_t *rgb) {
    JNIEnv  * env;
    bool needDetach = false;
    if (javaVm->GetEnv(reinterpret_cast<void **>(&env), JNI_VERSION_1_4) != JNI_OK) {
        if (javaVm->AttachCurrentThread(&env, nullptr) != JNI_OK) {
            return;
        }
        needDetach = true;
    }
    // rgb24 每像素3字节
    LOGD("callBackRGB24Data %d %d",width,height);
    jbyteArray jbyteRgb = env->NewByteArray(width * height * 3);
    env->SetByteArrayRegion(jbyteRgb, 0, width * height * 3, reinterpret_cast<const jbyte *>(rgb));
    env->CallVoidMethod(jobj, jmidCallRGB24, width, height, jbyteRgb);
    env->DeleteLocalRef(jbyteRgb);
    if (needDetach) {
        javaVm->DetachCurrentThread();
    }
}
void YsCallJava::callBackNV12Data(int width, int height, uint8_t *y, uint8_t *uv) {
    JNIEnv  * env;
    bool needDetach = false;
    // Check if we need to attach the current thread to the JVM
    //ffmpeg 硬解码由于需要调用jni 所以在我们回调之前已经进行了attach 。无需进行detach 因为ffmpeg 内部做了处理
    if (javaVm->GetEnv(reinterpret_cast<void **>(&env), JNI_VERSION_1_4) != JNI_OK) {
        if (javaVm->AttachCurrentThread(&env, nullptr) != JNI_OK) {
            return;
        }
        needDetach = true;
    }
    jbyteArray  jbyteY = env->NewByteArray(width*height);
    env->SetByteArrayRegion(jbyteY, 0,width*height, reinterpret_cast<const jbyte *>(y));

    jbyteArray  jbyteUv = env->NewByteArray(width*height/2);
    env->SetByteArrayRegion(jbyteUv, 0,width*height/2, reinterpret_cast<const jbyte *>(uv));

    env->CallVoidMethod(jobj,jmidCallNV12,width,height,jbyteY,jbyteUv);
    env->DeleteLocalRef(jbyteY);
    env->DeleteLocalRef(jbyteUv);
    if (needDetach) {
        javaVm->DetachCurrentThread();
    }
}

void YsCallJava::onCallTimeInfo(double currentPos, long duration) {
    JNIEnv *env;
    bool needDetach = false;
    if (javaVm->GetEnv(reinterpret_cast<void **>(&env), JNI_VERSION_1_4) != JNI_OK) {
        if (javaVm->AttachCurrentThread(&env, nullptr) != JNI_OK) {
            return;
        }
        needDetach = true;
    }
    env->CallVoidMethod(jobj, jmidCallTimeProgress,currentPos,duration);
    if (needDetach) {
        javaVm->DetachCurrentThread();
    }
}
