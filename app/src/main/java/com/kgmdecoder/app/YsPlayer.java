package com.kgmdecoder.app;

import android.os.Handler;
import android.os.Looper;
import android.util.Log;
import android.view.Surface;

/**
 * YsPlayer 核心实现（Java 版本）
 * 替代 YsPlayer.kt
 *
 * 功能：
 * - 通过 JNI 调用 C++ 层的 YsFFmpegPlayer
 * - 管理 SurfaceView 渲染（YsGLSurfaceView）
 * - 回调准备完成、尺寸变化、进度更新
 * - 支持硬解（MediaCodec）
 *
 * 使用方式：
 *   YsPlayer player = new YsPlayer();
 *   player.setSurfaceView(glSurfaceView);
 *   player.enableMediaCodec(true);
 *   player.prepare("https://example.com/video.mp4");
 *   player.setOnPrepareListener(() -> player.start());
 */
public class YsPlayer implements YsPlayerInterface {

    private static final String TAG = "YsPlayer";

    // Native 指针
    private long mNativePtr = 0;

    // 渲染视图
    private YsGLSurfaceView mSurfaceView;

    // 回调
    private Runnable mOnPrepareListener;
    private OnSizeChangeListener mOnSizeChangeListener;
    private OnProgressChangeListener mOnProgressChangeListener;

    // 主线程 Handler
    private final Handler mMainHandler = new Handler(Looper.getMainLooper());

    // 是否已释放
    private volatile boolean mReleased = false;

    static {
        try {
            System.loadLibrary("ysplayer");
        } catch (UnsatisfiedLinkError e) {
            Log.e(TAG, "加载 ysplayer 库失败", e);
        }
    }

    // ============================
    // Native 方法（对应 C++ 层 YsFFmpegPlayer）
    // ============================
    private native long nativeCreatePlayer();
    private native void nativeDestroyPlayer(long ptr);
    private native void nativeSetSurface(long ptr, Surface surface);
    private native void nativePrepare(long ptr, String url);
    private native void nativeStart(long ptr);
    private native void nativePause(long ptr);
    private native void nativeResume(long ptr);
    private native void nativeStop(long ptr);
    private native void nativeSeek(long ptr, int timeMills);
    private native void nativeEnableMediaCodec(long ptr, boolean enable);

    // ============================
    // 构造函数
    // ============================
    public YsPlayer() {
        mNativePtr = nativeCreatePlayer();
        if (mNativePtr == 0) {
            Log.e(TAG, "nativeCreatePlayer 失败");
        } else {
            Log.d(TAG, "YsPlayer 创建成功, ptr=" + mNativePtr);
        }
    }

    // ============================
    // 设置渲染视图
    // ============================
    @Override
    public void setSurfaceView(YsGLSurfaceView surfaceView) {
        mSurfaceView = surfaceView;
    }

    // ============================
    // 准备播放
    // ============================
    @Override
    public void prepare(String url) {
        if (mNativePtr == 0) {
            Log.e(TAG, "播放器未初始化");
            return;
        }
        Log.d(TAG, "prepare: " + url);
        nativePrepare(mNativePtr, url);
    }

    // ============================
    // 开始播放
    // ============================
    @Override
    public void start() {
        if (mNativePtr != 0) {
            nativeStart(mNativePtr);
        }
    }

    // ============================
    // 暂停
    // ============================
    @Override
    public void pause() {
        if (mNativePtr != 0) {
            nativePause(mNativePtr);
        }
    }

    // ============================
    // 恢复
    // ============================
    @Override
    public void resume() {
        if (mNativePtr != 0) {
            nativeResume(mNativePtr);
        }
    }

    // ============================
    // 停止
    // ============================
    @Override
    public void stop() {
        if (mNativePtr != 0) {
            nativeStop(mNativePtr);
        }
    }

    // ============================
    // 跳转
    // ============================
    @Override
    public void seek(int timeMills) {
        if (mNativePtr != 0) {
            nativeSeek(mNativePtr, timeMills);
        }
    }

    // ============================
    // 启用/禁用硬解
    // ============================
    @Override
    public void enableMediaCodec(boolean enable) {
        if (mNativePtr != 0) {
            nativeEnableMediaCodec(mNativePtr, enable);
        }
    }

    // ============================
    // 设置回调
    // ============================
    @Override
    public void setOnPrepareListener(Runnable listener) {
        mOnPrepareListener = listener;
    }

    @Override
    public void setOnSizeChangeListener(OnSizeChangeListener listener) {
        mOnSizeChangeListener = listener;
    }

    @Override
    public void setOnProgressChangeListener(OnProgressChangeListener listener) {
        mOnProgressChangeListener = listener;
    }

    // ============================
    // JNI 回调（C++ 层通过 CallVoidMethod 调用）
    // ============================

    /**
     * C++ 层 prepare 完成回调
     * 必须被 C++ 层以方法名 "onCallPrepare" 调用
     */
    public void onCallPrepare() {
        mMainHandler.post(() -> {
            if (mOnPrepareListener != null) {
                mOnPrepareListener.run();
            }
        });
    }

    /**
     * C++ 层视频尺寸变化回调
     */
    public void onCallVideoSize(int width, int height) {
        mMainHandler.post(() -> {
            if (mSurfaceView != null) {
                mSurfaceView.setVideoSize(width, height);
            }
            if (mOnSizeChangeListener != null) {
                mOnSizeChangeListener.onSizeChange(width, height);
            }
        });
    }

    /**
     * C++ 层进度回调
     */
    public void onCallProgress(double progress, long duration) {
        mMainHandler.post(() -> {
            if (mOnProgressChangeListener != null) {
                mOnProgressChangeListener.onProgress(progress, duration);
            }
        });
    }

    /**
     * C++ 层 YUV420P 数据回调
     * C++ 将解码后的 Y/U/V 分量通过 byte[] 传回 Java 层
     */
    public void onCallYuvData(int width, int height, byte[] y, byte[] u, byte[] v) {
        if (mSurfaceView != null) {
            mSurfaceView.setYuvData(width, height, y, u, v);
        }
    }

    /**
     * C++ 层 NV12 数据回调
     */
    public void onCallNV12Data(int width, int height, byte[] yData, byte[] uvData) {
        if (mSurfaceView != null) {
            mSurfaceView.setNV12Data(width, height, yData, uvData);
        }
    }

    // ============================
    // 设置 Surface（直接传入 Surface 对象）
    // ============================
    public void setSurface(Surface surface) {
        if (mNativePtr != 0) {
            nativeSetSurface(mNativePtr, surface);
        }
    }

    // ============================
    // 释放资源
    // ============================
    public void release() {
        if (mReleased) return;
        mReleased = true;

        if (mNativePtr != 0) {
            nativeStop(mNativePtr);
            nativeDestroyPlayer(mNativePtr);
            mNativePtr = 0;
        }

        Log.d(TAG, "YsPlayer 已释放");
    }

    @Override
    protected void finalize() throws Throwable {
        try {
            release();
        } finally {
            super.finalize();
        }
    }
}
