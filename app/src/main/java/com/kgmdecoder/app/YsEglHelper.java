package com.kgmdecoder.app;

import android.opengl.EGL14;
import android.opengl.EGLConfig;
import android.opengl.EGLContext;
import android.opengl.EGLDisplay;
import android.opengl.EGLSurface;
import android.opengl.GLSurfaceView;
import android.util.Log;
import android.view.Surface;

import javax.microedition.khronos.egl.EGL10;

/**
 * EGL 环境辅助类（Java 版本）
 * 替代 YsEglHelper.kt
 *
 * 功能：
 * - 初始化 EGL 显示、配置、上下文
 * - 创建 EGLWindowSurface 绑定到 Android Surface
 * - makeCurrent / swapBuffers / destroy
 *
 * 用于在独立线程中渲染 OpenGL ES（不依赖 GLSurfaceView 内置 EGL 管理）
 */
public class YsEglHelper {

    private static final String TAG = "YsEglHelper";

    // EGL 配置属性
    private static final int[] CONFIG_ATTRIBS = {
            EGL14.EGL_RED_SIZE, 8,
            EGL14.EGL_GREEN_SIZE, 8,
            EGL14.EGL_BLUE_SIZE, 8,
            EGL14.EGL_ALPHA_SIZE, 8,
            EGL14.EGL_DEPTH_SIZE, 0,
            EGL14.EGL_STENCIL_SIZE, 0,
            EGL14.EGL_RENDERABLE_TYPE, EGL14.EGL_OPENGL_ES2_BIT,
            EGL14.EGL_NONE
    };

    // EGL 上下文属性（OpenGL ES 2.0）
    private static final int[] CONTEXT_ATTRIBS = {
            EGL14.EGL_CONTEXT_CLIENT_VERSION, 2,
            EGL14.EGL_NONE
    };

    private EGLDisplay mEGLDisplay = EGL14.EGL_NO_DISPLAY;
    private EGLConfig mEGLConfig = null;
    private EGLContext mEGLContext = EGL14.EGL_NO_CONTEXT;
    private EGLSurface mEGLSurface = EGL14.EGL_NO_SURFACE;

    /**
     * 初始化 EGL 环境，并创建 Surface
     *
     * @param surface Android Surface（来自 SurfaceView 或 SurfaceTexture）
     */
    public void initEGL(Surface surface) {
        // 1. 获取 EGLDisplay
        mEGLDisplay = EGL14.eglGetDisplay(EGL14.EGL_DEFAULT_DISPLAY);
        if (mEGLDisplay == EGL14.EGL_NO_DISPLAY) {
            Log.e(TAG, "eglGetDisplay 失败");
            throw new RuntimeException("eglGetDisplay failed");
        }

        // 2. 初始化 EGL
        int[] version = new int[2];
        if (!EGL14.eglInitialize(mEGLDisplay, version, 0, version, 1)) {
            Log.e(TAG, "eglInitialize 失败");
            throw new RuntimeException("eglInitialize failed");
        }
        Log.d(TAG, "EGL 初始化成功, version=" + version[0] + "." + version[1]);

        // 3. 选择 EGLConfig
        EGLConfig[] configs = new EGLConfig[1];
        int[] numConfigs = new int[1];
        if (!EGL14.eglChooseConfig(mEGLDisplay, CONFIG_ATTRIBS, 0,
                configs, 0, 1, numConfigs, 0)) {
            Log.e(TAG, "eglChooseConfig 失败");
            throw new RuntimeException("eglChooseConfig failed");
        }
        mEGLConfig = configs[0];

        // 4. 创建 EGLContext
        mEGLContext = EGL14.eglCreateContext(mEGLDisplay, mEGLConfig,
                EGL14.EGL_NO_CONTEXT, CONTEXT_ATTRIBS, 0);
        if (mEGLContext == EGL14.EGL_NO_CONTEXT) {
            Log.e(TAG, "eglCreateContext 失败: " + EGL14.eglGetError());
            throw new RuntimeException("eglCreateContext failed");
        }

        // 5. 创建 EGLSurface（绑定到 Android Surface）
        int[] surfaceAttribs = { EGL14.EGL_NONE };
        mEGLSurface = EGL14.eglCreateWindowSurface(mEGLDisplay, mEGLConfig, surface, surfaceAttribs, 0);
        if (mEGLSurface == EGL14.EGL_NO_SURFACE) {
            Log.e(TAG, "eglCreateWindowSurface 失败: " + EGL14.eglGetError());
            throw new RuntimeException("eglCreateWindowSurface failed");
        }

        // 6. 设为当前上下文
        if (!EGL14.eglMakeCurrent(mEGLDisplay, mEGLSurface, mEGLSurface, mEGLContext)) {
            Log.e(TAG, "eglMakeCurrent 失败: " + EGL14.eglGetError());
            throw new RuntimeException("eglMakeCurrent failed");
        }

        Log.d(TAG, "EGL 环境初始化完成");
    }

    /**
     * 设为当前渲染上下文
     */
    public void makeCurrent() {
        if (mEGLDisplay != EGL14.EGL_NO_DISPLAY && mEGLSurface != EGL14.EGL_NO_SURFACE) {
            EGL14.eglMakeCurrent(mEGLDisplay, mEGLSurface, mEGLSurface, mEGLContext);
        }
    }

    /**
     * 交换缓冲区（显示渲染结果）
     */
    public void swapBuffers() {
        if (mEGLDisplay != EGL14.EGL_NO_DISPLAY && mEGLSurface != EGL14.EGL_NO_SURFACE) {
            EGL14.eglSwapBuffers(mEGLDisplay, mEGLSurface);
        }
    }

    /**
     * 销毁 EGL 环境
     */
    public void destroy() {
        if (mEGLDisplay != EGL14.EGL_NO_DISPLAY) {
            EGL14.eglMakeCurrent(mEGLDisplay,
                    EGL14.EGL_NO_SURFACE, EGL14.EGL_NO_SURFACE,
                    EGL14.EGL_NO_CONTEXT);

            if (mEGLSurface != EGL14.EGL_NO_SURFACE) {
                EGL14.eglDestroySurface(mEGLDisplay, mEGLSurface);
                mEGLSurface = EGL14.EGL_NO_SURFACE;
            }

            if (mEGLContext != EGL14.EGL_NO_CONTEXT) {
                EGL14.eglDestroyContext(mEGLDisplay, mEGLContext);
                mEGLContext = EGL14.EGL_NO_CONTEXT;
            }

            EGL14.eglTerminate(mEGLDisplay);
            mEGLDisplay = EGL14.EGL_NO_DISPLAY;
        }

        Log.d(TAG, "EGL 环境已销毁");
    }

    /**
     * 检查 EGL 是否已初始化
     */
    public boolean isReady() {
        return mEGLDisplay != EGL14.EGL_NO_DISPLAY
                && mEGLContext != EGL14.EGL_NO_CONTEXT
                && mEGLSurface != EGL14.EGL_NO_SURFACE;
    }
}
