package com.kgmdecoder.app;

import android.app.Activity;
import android.app.LocaleManager;
import android.content.Context;
import android.content.Intent;
import android.content.res.Configuration;
import android.net.Uri;
import android.os.Build;
import android.os.LocaleList;
import android.provider.DocumentsContract;
import android.os.Bundle;
import android.view.Gravity;
import android.view.GestureDetector;
import android.view.KeyEvent;
import android.view.MotionEvent;
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
import android.widget.Toast;
import android.util.Log;

import androidx.constraintlayout.widget.ConstraintLayout;
import androidx.core.view.WindowCompat;
import androidx.core.view.WindowInsetsCompat;
import androidx.core.view.WindowInsetsControllerCompat;

import com.google.android.material.textfield.TextInputLayout;
import java.io.File;
import java.util.Locale;

/**
 * MainActivity — 控制台 UI + 视频播放 Demo
 *
 * 修改：
 * 1. 添加 SurfaceView 用于视频渲染
 * 2. 添加 nativeSetSurface() JNI 方法
 * 3. 当 C++ 层播放视频时，显示 SurfaceView
 * 4. 新增获取App生效语言接口 getCurrentEffectiveLanguageTag()
 */
public class MainActivity extends Activity {
    private static final String TAG = "MainActivity";
    private static MainActivity sInstance;
    private TextView tvConsole;
    private static EditText etInput;
    private static TextInputLayout tilInput;
    private ScrollView svConsole;
    private ImageView ivClose;
    private ImageButton btnSetting;
    private static ImageView playArrow;
    private TextView btnPick;
    // 视频播放 SurfaceView
    private SurfaceView surfaceView;
    // 视频原始尺寸（用于自适应宽高比）
    private int videoWidth = 0;
    private int videoHeight = 0;
    // 是否处于横屏全屏模式
    private boolean isFullscreenLandscape = false;
    // 全屏时点击画面用于唤出的底部输入栏（及其分割线）
    private View inputBar;
    private View inputBarDivider;
    // 当前全屏下输入栏是否显示
    private boolean controlsVisible = true;

    // ====== Native 方法声明 ======
    public native void triggerCppToCallJava();
    public native void passInputToCpp(String input);
    public native void passEncoderConfig(String outputPath, String videoCodec, String audioCodec);
    public native void passCompressConfig(String outputPath, int level);
    public native void passTrimConfig(String outputPath, long startTimeUs, long endTimeUs);
    public native void passBrightnessConfig(String outputPath, short brightness, float gamma);
    // 传递应用常用路径（替代 C++ 侧硬编码 /sdcard、/storage/emulated/0 等）
    public native void passAppPaths(String externalStorageDir, String downloadDir, String filesDir);
    // 同步打开媒体文件（跳转 Selecting 前调用，使 hasVideo/hasAudio 生效）
    public native boolean nativeOpenFile(String path);
    // Surface JNI 方法
    public native void nativeSetSurface(Surface surface);
    public native void nativeShowVideoView(boolean show);
    public native void releaseFFmpeg();

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
            System.loadLibrary("tag");
            System.loadLibrary("ncm_dump");
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
            // 文件选择完成
            Uri uri = data.getData();
            String path = getRealPath(uri);
            if (path != null) {
                // KGM/NCM 加密文件：直接传给 C++ 解密，不弹 Selecting
                String lowerPath = path.toLowerCase();
                if (lowerPath.endsWith(".kgm") || lowerPath.endsWith(".kgm.flac") || lowerPath.endsWith(".ncm")) {
                    passInputToCpp(path);
                    return;
                }
                // 普通文件：nativeOpenFile + 弹 Selecting 选功能
                selectedFilePath = path;
                boolean opened = nativeOpenFile(path);
                Log.d(TAG, "预打开文件: " + path + " → " + (opened ? "成功" : "失败"));
                Intent intent = new Intent(this, Selecting.class);
                intent.putExtra("InputPath", path);
                startActivityForResult(intent, 200);
            }
        } else if (requestCode == 200) {
            // 来自 Selecting 的功能选择
            String function = data.getStringExtra("Function");
            if (function != null && selectedFilePath != null) {
                Log.d(TAG, "文件: " + selectedFilePath + " 功能: " + function);
                if ("EncodeWithOtherEncoders".equals(function)) {
                    // 编码功能需要额外参数：输出路径 + 视频/音频编码器名
                    String outputPath = data.getStringExtra("OutputPath");
                    String videoCodec = data.getStringExtra("VideoCodec");
                    String audioCodec = data.getStringExtra("AudioCodec");
                    Log.d(TAG, "编码参数: 输出=" + outputPath
                            + " 视频编码器=" + videoCodec
                            + " 音频编码器=" + audioCodec);
                    passEncoderConfig(outputPath,
                            videoCodec != null ? videoCodec : "",
                            audioCodec != null ? audioCodec : "");
                    passInputToCpp(selectedFilePath + "\n" + function);
                } else if ("EncodeWithCompress".equals(function)) {
                    String outputPath = data.getStringExtra("OutputPath");
                    String compressLevel = data.getStringExtra("CompressLevel");
                    Log.d(TAG, "压缩输出: 输出=" + outputPath + " 压缩等级=" + compressLevel);
                    int level = 5;
                    if (compressLevel.length() == 1) {
                        level = compressLevel.charAt(0) - '0';
                    }
                    if (compressLevel.length() == 2) {
                        level = 10;
                    }
                    passCompressConfig(outputPath, level);
                    passInputToCpp(selectedFilePath + "\n" + function);
                } else if ("EncodeWithTrim".equals(function)) {
                    String outputPath = data.getStringExtra("OutputPath");
                    final int AV_TIME_BASE = 1000000;
                    long startTimeUs = (long)(data.getDoubleExtra("StartTime", 0) * AV_TIME_BASE);
                    long endTimeUs   = (long)(data.getDoubleExtra("EndTime", 0)   * AV_TIME_BASE);
                    passTrimConfig(outputPath, startTimeUs, endTimeUs);
                    passInputToCpp(selectedFilePath + "\n" + function);
                } else if ("EncodeWithBrightness".equals(function)) {
                    String outputPath = data.getStringExtra("OutputPath");
                    short brightness = (short)(data.getIntExtra("Brightness", 0));
                    float gamma = data.getFloatExtra("Gamma", 1.0f);
                    passBrightnessConfig(outputPath, brightness, gamma);
                    passInputToCpp(selectedFilePath + "\n" + function);
                } else {
                    // 传给 C++ 处理，格式: 路径\n功能名
                    passInputToCpp(selectedFilePath + "\n" + function);
                }
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
        } catch (Exception ignored) {
        }
        try {
            if (DocumentsContract.isDocumentUri(this, uri)) {
                String docId = DocumentsContract.getDocumentId(uri);
                String[] split = docId.split(":");
                if ("primary".equalsIgnoreCase(split[0])) {
                    return "/storage/emulated/0/" + split[1];
                }
            }
        } catch (Exception ignored) {
        }
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

        // 传递应用常用路径给 C++ 侧（替代硬编码 /sdcard、/storage/emulated/0 等）
        String extStorage = android.os.Environment.getExternalStorageDirectory().getAbsolutePath();
        String downloadDir = android.os.Environment.getExternalStoragePublicDirectory(
                android.os.Environment.DIRECTORY_DOWNLOADS).getAbsolutePath();
        String filesDir = getFilesDir().getAbsolutePath();
        passAppPaths(extStorage, downloadDir, filesDir);

        tvConsole = findViewById(R.id.tv_console);
        svConsole = findViewById(R.id.sv_console);
        etInput = findViewById(R.id.et_input);
        tilInput = findViewById(R.id.til_input);
        ivClose = findViewById(R.id.iv_close);
        btnSetting = findViewById(R.id.btn_setting);
        btnPick = findViewById(R.id.btn_pick);
        surfaceView = findViewById(R.id.surface_view);
        playArrow = findViewById(R.id.playArrow);
        inputBar = findViewById(R.id.input_bar);
        inputBarDivider = findViewById(R.id.input_bar_divider);

        // 关闭按钮
        ivClose.setOnClickListener(new CloseButtonClickListener());
        // 输入框：回车确认
        etInput.setOnEditorActionListener(new InputEditorActionListener());
        etInput.setOnKeyListener(new InputKeyListener());
        // 选择文件按钮
        btnPick.setOnClickListener(new PickFileButtonClickListener());

        // SurfaceView 单击暂停/恢复 + 双击切换横竖屏
        GestureDetector gestureDetector = new GestureDetector(this, new GestureDetector.SimpleOnGestureListener() {
            @Override
            public boolean onSingleTapConfirmed(MotionEvent e) {
                clickTimestamp = System.currentTimeMillis();
                // 横屏独占全屏下：点击画面 → C++ 层暂停/恢复，同时切换底部输入框显隐
                if (isFullscreenLandscape) {
                    setControlsVisible(!controlsVisible);
                }
                return true;
            }
            @Override
            public boolean onDoubleTap(MotionEvent e) {
                toggleFullscreen();
                return true;
            }
        });
        surfaceView.setOnTouchListener((v, event) -> gestureDetector.onTouchEvent(event));

        btnSetting.setOnClickListener(v -> {
            Intent intent = new Intent(this, Help.class);
            startActivity(intent);
        });

        // ====== Surface 回调 ======
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

        //【新增】App启动获取当前生效语言
        String initLang = getCurrentEffectiveLanguageTag();
        Log.d(TAG, "init app language tag: " + initLang);

        // 启动 C++ 主线程
        triggerCppToCallJava();

        releaseFFmpeg();
    }

    /**
     * 获取App当前实际生效语言Tag
     * Android13+: 用户单独设置App语言则返回该语言；选择跟随系统，返回系统语言
     * @return 标准B‑47语言tag，永远不会返回null，示例："zh‑CN"、"en‑US"
     */
    public String getCurrentEffectiveLanguageTag() {
        Locale effectiveLocale;
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
            LocaleManager lm = (LocaleManager) getSystemService(Context.LOCALE_SERVICE);
            LocaleList appSetLocales = lm.getApplicationLocales();
            if (!appSetLocales.isEmpty()) {
                effectiveLocale = appSetLocales.get(0);
            } else {
                //跟随系统，取资源生效Locale
                effectiveLocale = getResources().getConfiguration().getLocales().get(0);
            }
        } else {
            //Android12及以下，无应用独立语言
            effectiveLocale = getResources().getConfiguration().getLocales().get(0);
        }
        return effectiveLocale.toLanguageTag();
    }

    /**
     * 监听配置变更：系统设置页修改App语言会触发此回调
     */
    @Override
    public void onConfigurationChanged(Configuration newConfig) {
        super.onConfigurationChanged(newConfig);
        String newLang = getCurrentEffectiveLanguageTag();
        Log.d(TAG, "config changed, new lang tag:" + newLang);
        // 屏幕旋转后重新计算 SurfaceView 尺寸
        if (surfaceView != null && surfaceView.getVisibility() == View.VISIBLE) {
            surfaceView.post(() -> updateSurfaceViewSize());
        }
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

    public static String callJavaGetLanguage() {
        if(sInstance == null){
            //Activity还没创建，兜底返回英文
            return "en";
        }
        //通过全局实例，调用【非静态实例方法】
        return sInstance.getCurrentEffectiveLanguageTag();
    }


    public static void callJavaSetETHintText(String text) {
        sInstance.runOnUiThread(() -> {
            tilInput.setHint(text);
        });
    }

    /**
     * C++ 层调用：播放状态变化时启用/禁用文件选择按钮
     * 播放中禁用 btn_pick，避免进入文件选择/Selecting 打断播放
     */
    public static void callJavaSetPlayingState(boolean playing) {
        if (sInstance != null && sInstance.btnPick != null) {
            sInstance.runOnUiThread(() -> {
                sInstance.btnPick.setEnabled(!playing);
                sInstance.btnPick.setAlpha(playing ? 0.4f : 1.0f);
            });
        }
    }

    /**
     * C++ 层调用：回调视频原始尺寸，用于自适应 SurfaceView 宽高比
     */
    public static void callJavaSetVideoSize(int width, int height) {
        if (sInstance != null) {
            sInstance.runOnUiThread(() -> {
                sInstance.videoWidth = width;
                sInstance.videoHeight = height;
                sInstance.updateSurfaceViewSize();
            });
        }
    }

    /**
     * 根据视频宽高比和当前可用区域，动态调整 SurfaceView 尺寸（fit-center，不拉伸）
     */
    private void updateSurfaceViewSize() {
        if (surfaceView == null) return;
        View parent = (View) surfaceView.getParent();
        if (parent == null || parent.getWidth() == 0 || parent.getHeight() == 0) return;

        int availableW = parent.getWidth();
        int availableH;
        if (isFullscreenLandscape) {
            // 横屏独占全屏：视频铺满整个父布局
            availableH = parent.getHeight();
        } else {
            // 窗口模式：底部到 input_bar 顶部为止，避免覆盖输入框；
            // 真实可用高度 = input_bar 顶部到 parent 顶部的距离（而不是整个 parent 高度）
            if (inputBar == null || inputBar.getHeight() == 0 || inputBar.getTop() <= 0) return;
            availableH = inputBar.getTop();
        }

        androidx.constraintlayout.widget.ConstraintLayout.LayoutParams lp =
                (androidx.constraintlayout.widget.ConstraintLayout.LayoutParams) surfaceView.getLayoutParams();

        // =========关键：清除全部旧垂直约束=========
        lp.topToTop = ConstraintLayout.LayoutParams.PARENT_ID;   // 顶部绑定父布局顶部
        lp.topToBottom = -1;
        if (isFullscreenLandscape) {
            lp.bottomToBottom = ConstraintLayout.LayoutParams.PARENT_ID; // 全屏：底部绑定父布局底部
            lp.bottomToTop = -1;
        } else {
            lp.bottomToBottom = -1;
            lp.bottomToTop = R.id.input_bar;                          // 否则绑定 input_bar 顶部
        }

        // 水平约束：左右绑定父布局
        lp.startToStart = ConstraintLayout.LayoutParams.PARENT_ID;
        lp.endToEnd = ConstraintLayout.LayoutParams.PARENT_ID;

        lp.width = ConstraintLayout.LayoutParams.MATCH_CONSTRAINT; // 0dp
        lp.height = ConstraintLayout.LayoutParams.MATCH_CONSTRAINT;// 0dp

        if (videoWidth <= 0 || videoHeight <= 0) {
            lp.dimensionRatio = null;
        } else {
            float videoRatio = (float) videoWidth / videoHeight;
            float availableRatio = (float) availableW / availableH;
            // ConstraintLayout 的 W/H 前缀指的是“被比例推导的那个维度”：
            //   "H,w:h" → 先按约束定宽，再按比例算高；"W,w:h" → 先按约束定高，再按比例算宽。
            if (videoRatio > availableRatio) {
                // 视频比可用区域更“宽”：以宽度为准（宽度受限，高度按比例缩小）
                lp.dimensionRatio = "H," + videoWidth + ":" + videoHeight;
            } else {
                // 视频更“高”：以高度为准（高度受限，宽度按比例缩小）
                lp.dimensionRatio = "W," + videoWidth + ":" + videoHeight;
            }
        }

        surfaceView.setLayoutParams(lp);
        surfaceView.requestLayout(); // 强制重新布局，非常重要
    }

    /**
     * 双击 SurfaceView 切换横屏全屏 / 竖屏
     */
    private void toggleFullscreen() {
        if (isFullscreenLandscape) {
            // 退出横屏独占全屏，回到竖屏
            setRequestedOrientation(android.content.pm.ActivityInfo.SCREEN_ORIENTATION_PORTRAIT);
            isFullscreenLandscape = false;
            exitImmersiveFullscreen();
            setControlsVisible(true);
        } else {
            // 进入横屏独占全屏（传感器方向，用户可正/反手持）
            setRequestedOrientation(android.content.pm.ActivityInfo.SCREEN_ORIENTATION_SENSOR_LANDSCAPE);
            isFullscreenLandscape = true;
            // 独占全屏：隐藏系统栏与底部输入栏，视频铺满整屏
            enterImmersiveFullscreen();
            setControlsVisible(false);
        }
    }

    /**
     * 进入独占全屏：隐藏系统状态栏/导航栏（下滑可临时唤出）
     */
    private void enterImmersiveFullscreen() {
        WindowInsetsControllerCompat controller =
                WindowCompat.getInsetsController(getWindow(), getWindow().getDecorView());
        controller.hide(WindowInsetsCompat.Type.systemBars());
        controller.setSystemBarsBehavior(
                WindowInsetsControllerCompat.BEHAVIOR_SHOW_TRANSIENT_BARS_BY_SWIPE);
    }

    /**
     * 退出独占全屏：恢复系统状态栏/导航栏
     */
    private void exitImmersiveFullscreen() {
        WindowInsetsControllerCompat controller =
                WindowCompat.getInsetsController(getWindow(), getWindow().getDecorView());
        controller.show(WindowInsetsCompat.Type.systemBars());
    }

    /**
     * 显示/隐藏底部输入栏。全屏下点击画面（暂停）时唤出，便于输入 stop 退出
     */
    private void setControlsVisible(boolean visible) {
        controlsVisible = visible;
        int visibility = visible ? View.VISIBLE : View.GONE;
        if (inputBar != null) inputBar.setVisibility(visibility);
        if (inputBarDivider != null) inputBarDivider.setVisibility(visibility);
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
                    if (show) {
                        // SurfaceView 从 GONE 变为 VISIBLE 后需等布局完成再算尺寸
                        sInstance.surfaceView.post(() -> sInstance.updateSurfaceViewSize());
                    } else {
                        // 播放结束：重置视频尺寸，恢复竖屏
                        sInstance.videoWidth = 0;
                        sInstance.videoHeight = 0;
                        if (sInstance.isFullscreenLandscape) {
                            sInstance.setRequestedOrientation(
                                    android.content.pm.ActivityInfo.SCREEN_ORIENTATION_PORTRAIT);
                            sInstance.isFullscreenLandscape = false;
                            sInstance.exitImmersiveFullscreen();
                            sInstance.setControlsVisible(true);
                        }
                    }
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
    }

    /** 播放进度回调 */
    public void onCallProgress(double currentPos, long duration) {
        Log.d(TAG, "YsPlayer: progress " + currentPos + " / " + duration);
    }

    /** RGB24 渲染数据 */
    public void onCallRGB24Data(int width, int height, byte[] rgb) {
        Log.d(TAG, "YsPlayer: onCallRGB24Data " + width + "x" + height);
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