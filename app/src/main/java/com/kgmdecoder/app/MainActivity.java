package com.kgmdecoder.app;

import android.app.Activity;
import android.content.Intent;
import android.net.Uri;
import android.provider.DocumentsContract;
import android.os.Bundle;
import android.view.Gravity;
import android.view.KeyEvent;
import android.view.Surface;
import android.view.SurfaceHolder;
import android.view.SurfaceView;
import android.view.View;
import android.view.inputmethod.EditorInfo;
import android.view.inputmethod.InputMethodManager;
import android.widget.EditText;
import android.widget.ImageButton;
import android.widget.ImageView;
import android.widget.ScrollView;
import android.widget.TextView;
import android.util.Log;

import java.io.File;

/**
 * MainActivity — 控制台 UI + 视频播放 Demo
 *
 * 修改：
 * 1. 添加 SurfaceView 用于视频渲染
 * 2. 添加 nativeSetSurface() JNI 方法
 * 3. 当 C++ 层播放视频时，显示 SurfaceView
 *
 * 测试用，后面要删掉重写
 */
public class MainActivity extends Activity {
    private static final String TAG = "MainActivity";
    private static MainActivity sInstance;

    private TextView tvConsole;
    private static EditText etInput;
    private ScrollView svConsole;
    private ImageView ivClose;
    private ImageButton btnSetting;
    private static ImageView playArrow;
    private TextView btnPick;

    // 视频播放 SurfaceView
    private SurfaceView surfaceView;

    // ====== Native 方法声明 ======
    public native void triggerCppToCallJava();
    public native void passInputToCpp(String input);

    // Surface JNI 方法（新增）
    public native void nativeSetSurface(Surface surface);
    public native void nativeShowVideoView(boolean show);

    // 普通变量
    private static long clickTimestamp = 0; // 点击时间戳, 0表示无效
    private static final long CLICK_TIME_OUT_MS = 300; // 按钮的超时时间
    private String selectedFilePath = null; // 选中的文件路径（等 Selecting 返回后使用）

    // ====== 静态加载本地库 ======
    static {
        try {
            System.loadLibrary("c++_shared");
            System.loadLibrary("z");
            System.loadLibrary("lzma");
            System.loadLibrary("kgm_decoder");
            System.loadLibrary("native");
        } catch (UnsatisfiedLinkError e) {
            Log.e("Library", "Failed to load native library", e);
            e.printStackTrace();
        }
    }

    // ============================
    // 文件选择 / 子 Activity 返回值回调
    // ============================
    @Override
    protected void onActivityResult(int requestCode, int resultCode, Intent data) {
        super.onActivityResult(requestCode, resultCode, data);
        if (resultCode != RESULT_OK || data == null) return;

        if (requestCode == 100) {
            // 文件选择完成 → 保存路径 → 跳转 Selecting 选功能
            Uri uri = data.getData();
            String path = getRealPath(uri);
            if (path != null) {
                selectedFilePath = path;
                Intent intent = new Intent(this, Selecting.class);
                startActivityForResult(intent, 200);
            }
        } else if (requestCode == 200) {
            // 来自 Selecting 的功能选择
            String function = data.getStringExtra("Function");
            if (function != null && selectedFilePath != null) {
                Log.d(TAG, "文件: " + selectedFilePath + " 功能: " + function);
                // 传给 C++ 处理，格式: 路径\n功能名
                passInputToCpp(selectedFilePath + "\n" + function);
                selectedFilePath = null;
            }
        }
    }

    /**
     * Uri 转真实路径
     */
    private String getRealPath(Uri uri) {
        if (uri == null) return null;
        try {
            String path = uri.getPath();
            if (path != null && path.startsWith("/storage")) {
                return path;
            }
        } catch (Exception ignored) { }
        try {
            if (DocumentsContract.isDocumentUri(this, uri)) {
                String docId = DocumentsContract.getDocumentId(uri);
                String[] split = docId.split(":");
                if ("primary".equalsIgnoreCase(split[0])) {
                    return "/storage/emulated/0/" + split[1];
                }
            }
        } catch (Exception ignored) { }
        return uri.toString();
    }

    // ============================
    // onCreate
    // ============================
    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        sInstance = this;

        setContentView(R.layout.activity_main);

        tvConsole = findViewById(R.id.tv_console);
        svConsole = findViewById(R.id.sv_console);
        etInput   = findViewById(R.id.et_input);
        ivClose   = findViewById(R.id.iv_close);
        btnSetting = findViewById(R.id.btn_setting);
        btnPick   = findViewById(R.id.btn_pick);
        surfaceView = findViewById(R.id.surface_view);
        playArrow = findViewById(R.id.playArrow);


        // 关闭按钮
        ivClose.setOnClickListener(new CloseButtonClickListener());

        // 输入框：回车确认
        etInput.setOnEditorActionListener(new InputEditorActionListener());
        etInput.setOnKeyListener(new InputKeyListener());

        // 选择文件按钮
        btnPick.setOnClickListener(new PickFileButtonClickListener());

        // Surface 点击回调(C++侧)
        surfaceView.setOnClickListener(v -> {
            clickTimestamp = System.currentTimeMillis();
        });

        btnSetting.setOnClickListener(v -> {
            Intent intent = new Intent(this, Help.class);
            startActivity(intent);
        });

        // ====== Surface 回调（新增） ======
        surfaceView.getHolder().addCallback(new SurfaceHolder.Callback() {
            @Override
            public void surfaceCreated(SurfaceHolder holder) {
                Log.d(TAG, "SurfaceView surfaceCreated");
                nativeSetSurface(holder.getSurface());
            }

            @Override
            public void surfaceChanged(SurfaceHolder holder, int format, int width, int height) {
                Log.d(TAG, "SurfaceView surfaceChanged: " + width + "x" + height);
            }

            @Override
            public void surfaceDestroyed(SurfaceHolder holder) {
                Log.d(TAG, "SurfaceView surfaceDestroyed");
                nativeSetSurface(null);
            }
        });

        surfaceView.callOnClick();

        // 启动 C++ 主线程
        triggerCppToCallJava();
    }

    // ============================
    // 静态回调（供 C++ 通过 JNI 调用）
    // ============================
    public static void showText(final String text) {
        if (sInstance != null && sInstance.tvConsole != null) {
            sInstance.runOnUiThread(new ShowTextRunnable(text));
        }
    }

    public static void getJavaInput() {
        if (sInstance != null && sInstance.etInput != null) {
            sInstance.runOnUiThread(new GetInputRunnable());
        }
    }

    public static void callJavaClear() {
        if (sInstance != null && sInstance.tvConsole != null) {
            sInstance.runOnUiThread(new ClearConsoleRunnable());
        }
    }

    public static void callJavaExit() {
        if (sInstance != null) {
            sInstance.runOnUiThread(new ExitAppRunnable());
        }
    }

    public static boolean callJavaIsSurfaceClicked() {
        if (clickTimestamp == 0) return false;
        long now = System.currentTimeMillis();
        if (now - clickTimestamp <= CLICK_TIME_OUT_MS) {
            clickTimestamp = 0;
            return true;
        } else {
            clickTimestamp = 0;
            return false;
        }
    }

    public static void callJavaControlPlayArrowVisibility(boolean visibility) {
        if (sInstance != null) {
            sInstance.runOnUiThread(() -> {
                if (playArrow != null) {
                    playArrow.setVisibility(visibility ? View.VISIBLE : View.GONE);
                    if (visibility) {
                        playArrow.bringToFront();
                        playArrow.requestLayout();
                    }
                }
            });
        }
    }

    public static void callJavaSetETHintText(String text) {
        sInstance.runOnUiThread(() -> {
            etInput.setHint(text);
        });
    }

    /**
     * C++ 层调用：显示/隐藏视频播放视图
     * 当 C++ 开始播放视频时调用 showVideoView(true)
     * 播放结束时调用 showVideoView(false)
     */
    public static void showVideoView(final boolean show) {
        if (sInstance != null) {
            sInstance.runOnUiThread(() -> {
                if (sInstance.surfaceView != null) {
                    sInstance.surfaceView.setVisibility(
                            show ? View.VISIBLE : View.GONE);
                    Log.d(TAG, "SurfaceView visibility: " + (show ? "VISIBLE" : "GONE"));
                }
            });
        }
    }

    // ============================
    // YsPlayer 回调方法（YsCallJava 通过 JNI GetMethodID 查找）
    // 必须是非静态实例方法
    // ============================

    /** 准备完成 */
    public void onCallPrepare() {
        Log.d(TAG, "YsPlayer: onCallPrepare 准备完成");
    }

    /** YUV420P 软解数据 — C++ 层已直接渲染到 ANativeWindow，Java 端无需处理 */
    public void onCallYuvData(int width, int height, byte[] y, byte[] u, byte[] v) {
        // 视频帧已在 C++ 层通过 sws_scale + ANativeWindow 直接渲染
        // 此方法保留是因为 YsCallJava 构造函数需要 GetMethodID 找到它
    }

    /** NV12 硬解数据 */
    public void onCallNV12Data(int width, int height, byte[] y, byte[] uv) {
        Log.d(TAG, "YsPlayer: onCallNV12Data " + width + "x" + height);
        // TODO: 将 NV12 数据传递给 GLSurfaceView 渲染
    }

    /** 播放进度回调 */
    public void onCallProgress(double currentPos, long duration) {
        Log.d(TAG, "YsPlayer: progress " + currentPos + " / " + duration);
    }

    /** RGB24 渲染数据 */
    public void onCallRGB24Data(int width, int height, byte[] rgb) {
        Log.d(TAG, "YsPlayer: onCallRGB24Data " + width + "x" + height);
        // TODO: 将 RGB24 数据传递给 SurfaceView 渲染
    }

    @Override
    protected void onDestroy() {
        super.onDestroy();
        sInstance = null;
    }

    // ============================
    // 命名内部类
    // ============================
    private static class CloseButtonClickListener implements View.OnClickListener {
        @Override
        public void onClick(View v) {
            sInstance.finish();
            System.exit(0);
        }
    }

    private static class InputEditorActionListener implements TextView.OnEditorActionListener {
        @Override
        public boolean onEditorAction(TextView v, int actionId, KeyEvent event) {
            boolean isEnterKey = false;
            if (event != null) {
                if (event.getKeyCode() == KeyEvent.KEYCODE_ENTER) {
                    isEnterKey = true;
                }
            } else {
                if (actionId == EditorInfo.IME_ACTION_DONE ||
                        actionId == EditorInfo.IME_ACTION_NEXT ||
                        actionId == EditorInfo.IME_ACTION_SEND ||
                        actionId == EditorInfo.IME_ACTION_UNSPECIFIED) {
                    isEnterKey = true;
                }
            }
            if (isEnterKey) {
                String input = sInstance.etInput.getText().toString().trim();
                if (!input.isEmpty()) {
                    sInstance.passInputToCpp(input);
                    sInstance.etInput.setText("");
                }
                InputMethodManager imm = (InputMethodManager) sInstance.getSystemService(INPUT_METHOD_SERVICE);
                if (imm != null) {
                    imm.hideSoftInputFromWindow(sInstance.etInput.getWindowToken(), 0);
                }
                return true;
            }
            return false;
        }
    }

    private static class InputKeyListener implements View.OnKeyListener {
        @Override
        public boolean onKey(View v, int keyCode, KeyEvent event) {
            if (keyCode == KeyEvent.KEYCODE_ENTER && event.getAction() == KeyEvent.ACTION_UP) {
                String input = sInstance.etInput.getText().toString().trim();
                if (!input.isEmpty()) {
                    sInstance.passInputToCpp(input);
                    sInstance.etInput.setText("");
                }
                InputMethodManager imm = (InputMethodManager) sInstance.getSystemService(INPUT_METHOD_SERVICE);
                if (imm != null) {
                    imm.hideSoftInputFromWindow(sInstance.etInput.getWindowToken(), 0);
                }
                return true;
            }
            return false;
        }
    }

    private static class PickFileButtonClickListener implements View.OnClickListener {
        @Override
        public void onClick(View v) {
            Intent intent = new Intent(Intent.ACTION_OPEN_DOCUMENT);
            intent.addCategory(Intent.CATEGORY_OPENABLE);
            intent.setType("*/*");
            sInstance.startActivityForResult(intent, 100);
        }
    }

    private static class ShowTextRunnable implements Runnable {
        private final String text;
        public ShowTextRunnable(String text) {
            this.text = text;
        }
        @Override
        public void run() {
            sInstance.tvConsole.append(text + "\n");
            sInstance.svConsole.post(() -> sInstance.svConsole.fullScroll(View.FOCUS_DOWN));
        }
    }

    private static class GetInputRunnable implements Runnable {
        @Override
        public void run() {
            sInstance.etInput.requestFocus();
        }
    }

    private static class ClearConsoleRunnable implements Runnable {
        @Override
        public void run() {
            sInstance.tvConsole.setText("");
        }
    }

    private static class ExitAppRunnable implements Runnable {
        @Override
        public void run() {
            sInstance.finish();
            System.exit(0);
        }
    }
}
