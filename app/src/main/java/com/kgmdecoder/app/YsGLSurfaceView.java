package com.kgmdecoder.app;

import android.content.Context;
import android.opengl.GLES20;
import android.util.Log;
import android.view.SurfaceHolder;
import android.view.SurfaceView;

import java.nio.ByteBuffer;
import java.nio.ByteOrder;
import java.nio.FloatBuffer;

/**
 * OpenGL ES 视频渲染视图（Java 版本）
 * 替代 YsGLSurfaceView.kt
 *
 * 功能：
 * - 自管理渲染线程（不依赖 GLSurfaceView 的 Renderer 回调）
 * - 通过 YsEglHelper 初始化 EGL 环境
 * - 接收 YUV420P / NV12 数据并渲染到屏幕
 * - 自动适配视频尺寸（保持宽高比）
 *
 * 使用方式：
 *   YsGLSurfaceView glView = new YsGLSurfaceView(context);
 *   // C++ 层解码后调用 setYuvData() 或 setNV12Data()
 *   glView.setVideoSize(1920, 1080);
 *   glView.setRenderType(RENDER_TYPE_YUV420P);
 */
public class YsGLSurfaceView extends SurfaceView implements SurfaceHolder.Callback {

    private static final String TAG = "YsGLSurfaceView";

    // 渲染类型
    public static final int RENDER_TYPE_YUV420P = 0;  // 三平面 YUV
    public static final int RENDER_TYPE_NV12    = 1;  // 两平面（Y + UV交错）

    // ============================
    // 顶点着色器（YUV420P 和 NV12 通用）
    // ============================
    private static final String VERTEX_SHADER =
            "attribute vec4 aPosition;\n" +
            "attribute vec2 aTexCoord;\n" +
            "varying vec2 vTexCoord;\n" +
            "void main() {\n" +
            "    gl_Position = aPosition;\n" +
            "    vTexCoord = aTexCoord;\n" +
            "}\n";

    // ============================
    // YUV420P 片段着色器（3 个纹理：Y, U, V）
    // ============================
    private static final String FRAGMENT_SHADER_YUV420P =
            "precision mediump float;\n" +
            "varying vec2 vTexCoord;\n" +
            "uniform sampler2D yTexture;\n" +
            "uniform sampler2D uTexture;\n" +
            "uniform sampler2D vTexture;\n" +
            "void main() {\n" +
            "    float y = texture2D(yTexture, vTexCoord).r;\n" +
            "    float u = texture2D(uTexture, vTexCoord).r - 0.5;\n" +
            "    float v = texture2D(vTexture, vTexCoord).r - 0.5;\n" +
            "    float r = y + 1.402 * v;\n" +
            "    float g = y - 0.344 * u - 0.714 * v;\n" +
            "    float b = y + 1.772 * u;\n" +
            "    gl_FragColor = vec4(r, g, b, 1.0);\n" +
            "}\n";

    // ============================
    // NV12 片段着色器（2 个纹理：Y, UV）
    // ============================
    private static final String FRAGMENT_SHADER_NV12 =
            "precision mediump float;\n" +
            "varying vec2 vTexCoord;\n" +
            "uniform sampler2D yTexture;\n" +
            "uniform sampler2D uvTexture;\n" +
            "void main() {\n" +
            "    float y = texture2D(yTexture, vTexCoord).r;\n" +
            "    vec2 uv = texture2D(uvTexture, vTexCoord).ra - vec2(0.5, 0.5);\n" +
            "    float u = uv.x;\n" +
            "    float v = uv.y;\n" +
            "    float r = y + 1.402 * v;\n" +
            "    float g = y - 0.344 * u - 0.714 * v;\n" +
            "    float b = y + 1.772 * u;\n" +
            "    gl_FragColor = vec4(r, g, b, 1.0);\n" +
            "}\n";

    // ============================
    // 顶点坐标（全屏四边形）
    // ============================
    private static final float[] VERTEX_COORDS = {
            -1.0f, -1.0f,   // 左下
             1.0f, -1.0f,   // 右下
            -1.0f,  1.0f,   // 左上
             1.0f,  1.0f,   // 右上
    };

    // ============================
    // 纹理坐标
    // ============================
    private static final float[] TEX_COORDS = {
            0.0f, 1.0f,   // 左下
            1.0f, 1.0f,   // 右下
            0.0f, 0.0f,   // 左上
            1.0f, 0.0f,   // 右上
    };

    // ============================
    // 成员变量
    // ============================
    private final YsEglHelper mEglHelper = new YsEglHelper();
    private RenderThread mRenderThread;
    private volatile boolean mSurfaceAvailable = false;

    private int mVideoWidth = 0;
    private int mVideoHeight = 0;
    private int mRenderType = RENDER_TYPE_YUV420P;

    // GL 程序和属性
    private volatile int mProgram = 0;
    private volatile int mAttribPosition;
    private volatile int mAttribTexCoord;
    private volatile int mUniformYTexture;
    private volatile int mUniformUTexture;
    private volatile int mUniformVTexture;
    private volatile int mUniformUVTexture;

    // 纹理 ID
    private volatile int[] mTextures = new int[3];

    // 当前帧数据
    private final Object mFrameLock = new Object();
    private byte[] mYData;
    private byte[] mUData;
    private byte[] mVData;
    private byte[] mUVData;
    private volatile boolean mNewFrame = false;

    // 回调
    private OnSurfaceCreatedListener mOnSurfaceCreatedListener;

    /**
     * Surface 创建回调
     */
    public interface OnSurfaceCreatedListener {
        void onSurfaceCreated();
    }

    public YsGLSurfaceView(Context context) {
        super(context);
        getHolder().addCallback(this);
    }

    public void setOnSurfaceCreatedListener(OnSurfaceCreatedListener listener) {
        mOnSurfaceCreatedListener = listener;
    }

    /**
     * 设置视频尺寸
     */
    public void setVideoSize(int width, int height) {
        mVideoWidth = width;
        mVideoHeight = height;
    }

    /**
     * 设置渲染类型
     */
    public void setRenderType(int type) {
        mRenderType = type;
    }

    /**
     * 设置 YUV420P 数据（C++ 层解码后调用）
     */
    public void setYuvData(int width, int height, byte[] y, byte[] u, byte[] v) {
        synchronized (mFrameLock) {
            mVideoWidth = width;
            mVideoHeight = height;
            mYData = y;
            mUData = u;
            mVData = v;
            mNewFrame = true;
            mFrameLock.notifyAll();
        }
    }

    /**
     * 设置 NV12 数据（C++ 层解码后调用）
     */
    public void setNV12Data(int width, int height, byte[] yData, byte[] uvData) {
        synchronized (mFrameLock) {
            mVideoWidth = width;
            mVideoHeight = height;
            mYData = yData;
            mUVData = uvData;
            mNewFrame = true;
            mFrameLock.notifyAll();
        }
    }

    // ============================
    // SurfaceHolder.Callback
    // ============================
    @Override
    public void surfaceCreated(SurfaceHolder holder) {
        Log.d(TAG, "surfaceCreated");
        mSurfaceAvailable = true;
        mRenderThread = new RenderThread();
        mRenderThread.start();
    }

    @Override
    public void surfaceChanged(SurfaceHolder holder, int format, int width, int height) {
        Log.d(TAG, "surfaceChanged: " + width + "x" + height);
    }

    @Override
    public void surfaceDestroyed(SurfaceHolder holder) {
        Log.d(TAG, "surfaceDestroyed");
        mSurfaceAvailable = false;
        if (mRenderThread != null) {
            mRenderThread.requestExit();
            try {
                mRenderThread.join();
            } catch (InterruptedException e) {
                Thread.currentThread().interrupt();
            }
            mRenderThread = null;
        }
    }

    // ============================
    // 渲染线程
    // ============================
    private class RenderThread extends Thread {

        private volatile boolean mRunning = true;

        void requestExit() {
            mRunning = false;
            synchronized (mFrameLock) {
                mFrameLock.notifyAll();
            }
        }

        @Override
        public void run() {
            // 1. 初始化 EGL
            mEglHelper.initEGL(getHolder().getSurface());

            // 2. 初始化 OpenGL ES
            initGL();

            // 通知 Surface 已创建
            if (mOnSurfaceCreatedListener != null) {
                mOnSurfaceCreatedListener.onSurfaceCreated();
            }

            // 3. 渲染循环
            while (mRunning) {
                byte[] yData = null, uData = null, vData = null, uvData = null;
                int width = 0, height = 0;

                synchronized (mFrameLock) {
                    while (mRunning && !mNewFrame) {
                        try {
                            mFrameLock.wait(33);  // ~30fps
                        } catch (InterruptedException e) {
                            Thread.currentThread().interrupt();
                            return;
                        }
                    }
                    if (!mRunning) break;

                    width = mVideoWidth;
                    height = mVideoHeight;
                    if (mRenderType == RENDER_TYPE_YUV420P) {
                        yData = mYData;
                        uData = mUData;
                        vData = mVData;
                    } else {
                        yData = mYData;
                        uvData = mUVData;
                    }
                    mNewFrame = false;
                }

                if (yData != null && width > 0 && height > 0) {
                    drawFrame(width, height, yData, uData, vData, uvData);
                    mEglHelper.swapBuffers();
                }
            }

            // 4. 清理 GL 资源
            cleanupGL();
            mEglHelper.destroy();
        }

        private void initGL() {
            // 创建着色器程序
            String fragShader = (mRenderType == RENDER_TYPE_YUV420P)
                    ? FRAGMENT_SHADER_YUV420P
                    : FRAGMENT_SHADER_NV12;

            mProgram = YsShaderUtil.createProgram(VERTEX_SHADER, fragShader);
            if (mProgram == 0) {
                Log.e(TAG, "着色器程序创建失败");
                return;
            }

            // 获取属性位置
            mAttribPosition = GLES20.glGetAttribLocation(mProgram, "aPosition");
            mAttribTexCoord = GLES20.glGetAttribLocation(mProgram, "aTexCoord");

            // 获取 uniform 位置
            mUniformYTexture = GLES20.glGetUniformLocation(mProgram, "yTexture");
            if (mRenderType == RENDER_TYPE_YUV420P) {
                mUniformUTexture = GLES20.glGetUniformLocation(mProgram, "uTexture");
                mUniformVTexture = GLES20.glGetUniformLocation(mProgram, "vTexture");
            } else {
                mUniformUVTexture = GLES20.glGetUniformLocation(mProgram, "uvTexture");
            }

            // 创建纹理
            int texCount = (mRenderType == RENDER_TYPE_YUV420P) ? 3 : 2;
            mTextures = new int[texCount];
            GLES20.glGenTextures(texCount, mTextures, 0);

            for (int i = 0; i < texCount; i++) {
                GLES20.glBindTexture(GLES20.GL_TEXTURE_2D, mTextures[i]);
                GLES20.glTexParameteri(GLES20.GL_TEXTURE_2D, GLES20.GL_TEXTURE_MIN_FILTER, GLES20.GL_LINEAR);
                GLES20.glTexParameteri(GLES20.GL_TEXTURE_2D, GLES20.GL_TEXTURE_MAG_FILTER, GLES20.GL_LINEAR);
                GLES20.glTexParameteri(GLES20.GL_TEXTURE_2D, GLES20.GL_TEXTURE_WRAP_S, GLES20.GL_CLAMP_TO_EDGE);
                GLES20.glTexParameteri(GLES20.GL_TEXTURE_2D, GLES20.GL_TEXTURE_WRAP_T, GLES20.GL_CLAMP_TO_EDGE);
            }

            Log.d(TAG, "GL 初始化完成, program=" + mProgram);
        }

        private void drawFrame(int width, int height,
                               byte[] yData, byte[] uData, byte[] vData, byte[] uvData) {
            GLES20.glViewport(0, 0, getWidth(), getHeight());
            GLES20.glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
            GLES20.glClear(GLES20.GL_COLOR_BUFFER_BIT);

            GLES20.glUseProgram(mProgram);

            // 顶点坐标
            FloatBuffer vertexBuffer = ByteBuffer.allocateDirect(VERTEX_COORDS.length * 4)
                    .order(ByteOrder.nativeOrder())
                    .asFloatBuffer();
            vertexBuffer.put(VERTEX_COORDS).position(0);
            GLES20.glVertexAttribPointer(mAttribPosition, 2, GLES20.GL_FLOAT, false, 0, vertexBuffer);
            GLES20.glEnableVertexAttribArray(mAttribPosition);

            // 纹理坐标
            FloatBuffer texBuffer = ByteBuffer.allocateDirect(TEX_COORDS.length * 4)
                    .order(ByteOrder.nativeOrder())
                    .asFloatBuffer();
            texBuffer.put(TEX_COORDS).position(0);
            GLES20.glVertexAttribPointer(mAttribTexCoord, 2, GLES20.GL_FLOAT, false, 0, texBuffer);
            GLES20.glEnableVertexAttribArray(mAttribTexCoord);

            if (mRenderType == RENDER_TYPE_YUV420P) {
                // Y 纹理
                GLES20.glActiveTexture(GLES20.GL_TEXTURE0);
                GLES20.glBindTexture(GLES20.GL_TEXTURE_2D, mTextures[0]);
                GLES20.glTexImage2D(GLES20.GL_TEXTURE_2D, 0, GLES20.GL_LUMINANCE,
                        width, height, 0, GLES20.GL_LUMINANCE, GLES20.GL_UNSIGNED_BYTE,
                        ByteBuffer.wrap(yData));
                GLES20.glUniform1i(mUniformYTexture, 0);

                // U 纹理
                GLES20.glActiveTexture(GLES20.GL_TEXTURE1);
                GLES20.glBindTexture(GLES20.GL_TEXTURE_2D, mTextures[1]);
                GLES20.glTexImage2D(GLES20.GL_TEXTURE_2D, 0, GLES20.GL_LUMINANCE,
                        width / 2, height / 2, 0, GLES20.GL_LUMINANCE, GLES20.GL_UNSIGNED_BYTE,
                        ByteBuffer.wrap(uData));
                GLES20.glUniform1i(mUniformUTexture, 1);

                // V 纹理
                GLES20.glActiveTexture(GLES20.GL_TEXTURE2);
                GLES20.glBindTexture(GLES20.GL_TEXTURE_2D, mTextures[2]);
                GLES20.glTexImage2D(GLES20.GL_TEXTURE_2D, 0, GLES20.GL_LUMINANCE,
                        width / 2, height / 2, 0, GLES20.GL_LUMINANCE, GLES20.GL_UNSIGNED_BYTE,
                        ByteBuffer.wrap(vData));
                GLES20.glUniform1i(mUniformVTexture, 2);
            } else {
                // NV12: Y 纹理
                GLES20.glActiveTexture(GLES20.GL_TEXTURE0);
                GLES20.glBindTexture(GLES20.GL_TEXTURE_2D, mTextures[0]);
                GLES20.glTexImage2D(GLES20.GL_TEXTURE_2D, 0, GLES20.GL_LUMINANCE,
                        width, height, 0, GLES20.GL_LUMINANCE, GLES20.GL_UNSIGNED_BYTE,
                        ByteBuffer.wrap(yData));
                GLES20.glUniform1i(mUniformYTexture, 0);

                // NV12: UV 交错纹理
                GLES20.glActiveTexture(GLES20.GL_TEXTURE1);
                GLES20.glBindTexture(GLES20.GL_TEXTURE_2D, mTextures[1]);
                GLES20.glTexImage2D(GLES20.GL_TEXTURE_2D, 0, GLES20.GL_LUMINANCE_ALPHA,
                        width / 2, height / 2, 0, GLES20.GL_LUMINANCE_ALPHA, GLES20.GL_UNSIGNED_BYTE,
                        ByteBuffer.wrap(uvData));
                GLES20.glUniform1i(mUniformUVTexture, 1);
            }

            // 绘制四边形
            GLES20.glDrawArrays(GLES20.GL_TRIANGLE_STRIP, 0, 4);

            GLES20.glDisableVertexAttribArray(mAttribPosition);
            GLES20.glDisableVertexAttribArray(mAttribTexCoord);
        }

        private void cleanupGL() {
            if (mProgram != 0) {
                GLES20.glDeleteProgram(mProgram);
                mProgram = 0;
            }
            if (mTextures != null && mTextures.length > 0) {
                GLES20.glDeleteTextures(mTextures.length, mTextures, 0);
            }
            Log.d(TAG, "GL 资源已清理");
        }
    }
}
