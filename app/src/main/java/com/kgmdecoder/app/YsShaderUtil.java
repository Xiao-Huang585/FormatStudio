package com.kgmdecoder.app;

import android.opengl.GLES20;
import android.util.Log;

import java.io.ByteArrayOutputStream;
import java.io.IOException;
import java.io.InputStream;

/**
 * 着色器工具类（Java 版本）
 * 替代 YsShaderUtil.kt
 *
 * 功能：
 * - 从 assets 读取着色器源码
 * - 编译顶点/片段着色器
 * - 链接 OpenGL ES 程序
 */
public final class YsShaderUtil {

    private static final String TAG = "YsShaderUtil";

    private YsShaderUtil() {}

    /**
     * 从 assets 读取文本文件
     *
     * @param assets  AssetManager
     * @param name    文件名（如 "vertex_shader.glsl"）
     * @return 文件内容字符串
     */
    public static String readRawTextFromAssets(android.content.res.AssetManager assets, String name) {
        InputStream is = null;
        ByteArrayOutputStream baos = null;
        try {
            is = assets.open(name);
            baos = new ByteArrayOutputStream();
            byte[] buffer = new byte[1024];
            int len;
            while ((len = is.read(buffer)) != -1) {
                baos.write(buffer, 0, len);
            }
            return baos.toString("UTF-8");
        } catch (IOException e) {
            Log.e(TAG, "读取 assets 文件失败: " + name, e);
            return null;
        } finally {
            try {
                if (is != null) is.close();
                if (baos != null) baos.close();
            } catch (IOException ignored) {}
        }
    }

    /**
     * 编译着色器
     *
     * @param type       着色器类型（GLES20.GL_VERTEX_SHADER 或 GLES20.GL_FRAGMENT_SHADER）
     * @param shaderCode 着色器源码
     * @return 着色器 ID（失败返回 0）
     */
    public static int loadShader(int type, String shaderCode) {
        int shader = GLES20.glCreateShader(type);
        if (shader == 0) {
            Log.e(TAG, "glCreateShader 失败, type=" + type);
            return 0;
        }
        GLES20.glShaderSource(shader, shaderCode);
        GLES20.glCompileShader(shader);

        int[] compiled = new int[1];
        GLES20.glGetShaderiv(shader, GLES20.GL_COMPILE_STATUS, compiled, 0);
        if (compiled[0] == GLES20.GL_FALSE) {
            String log = GLES20.glGetShaderInfoLog(shader);
            Log.e(TAG, "着色器编译失败:\n" + shaderCode + "\n" + log);
            GLES20.glDeleteShader(shader);
            return 0;
        }
        return shader;
    }

    /**
     * 创建并链接 OpenGL ES 程序
     *
     * @param vertexSource   顶点着色器源码
     * @param fragmentSource 片段着色器源码
     * @return 程序 ID（失败返回 0）
     */
    public static int createProgram(String vertexSource, String fragmentSource) {
        int vertexShader = loadShader(GLES20.GL_VERTEX_SHADER, vertexSource);
        if (vertexShader == 0) return 0;

        int fragmentShader = loadShader(GLES20.GL_FRAGMENT_SHADER, fragmentSource);
        if (fragmentShader == 0) {
            GLES20.glDeleteShader(vertexShader);
            return 0;
        }

        int program = GLES20.glCreateProgram();
        if (program == 0) {
            Log.e(TAG, "glCreateProgram 失败");
            GLES20.glDeleteShader(vertexShader);
            GLES20.glDeleteShader(fragmentShader);
            return 0;
        }

        GLES20.glAttachShader(program, vertexShader);
        GLES20.glAttachShader(program, fragmentShader);
        GLES20.glLinkProgram(program);

        int[] linked = new int[1];
        GLES20.glGetProgramiv(program, GLES20.GL_LINK_STATUS, linked, 0);
        if (linked[0] == GLES20.GL_FALSE) {
            String log = GLES20.glGetProgramInfoLog(program);
            Log.e(TAG, "程序链接失败: " + log);
            GLES20.glDeleteProgram(program);
            program = 0;
        }

        // 链接后可删除着色器
        GLES20.glDeleteShader(vertexShader);
        GLES20.glDeleteShader(fragmentShader);

        return program;
    }
}
