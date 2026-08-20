package com.kgmdecoder.app;

/**
 * YsPlayer 接口定义（Java 版本）
 * 对应 YsPlayer.kt 的 YsPlayerInterface
 */
public interface YsPlayerInterface {
    void setSurfaceView(YsGLSurfaceView surfaceView);
    void prepare(String url);
    void start();
    void pause();
    void resume();
    void stop();
    void seek(int timeMills);
    void setOnPrepareListener(Runnable listener);
    void setOnSizeChangeListener(OnSizeChangeListener listener);
    void setOnProgressChangeListener(OnProgressChangeListener listener);
    void enableMediaCodec(boolean enable);

    interface OnSizeChangeListener {
        void onSizeChange(int w, int h);
    }

    interface OnProgressChangeListener {
        void onProgress(double progress, long duration);
    }
}
