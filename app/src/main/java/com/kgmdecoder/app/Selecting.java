package com.kgmdecoder.app;

import android.app.Activity;
import android.content.Intent;
import android.os.Bundle;
import android.view.LayoutInflater;
import android.view.View;
import android.view.ViewGroup;
import android.view.inputmethod.EditorInfo;
import android.view.animation.AccelerateInterpolator;
import android.view.animation.DecelerateInterpolator;
import android.widget.AdapterView;
import android.widget.ArrayAdapter;
import android.widget.Button;
import android.widget.EditText;
import android.widget.FrameLayout;
import android.widget.ImageButton;
import android.widget.LinearLayout;
import android.widget.Spinner;
import android.widget.TextView;
import android.widget.Toast;

enum FunctionWindow {
    METADATA, BRIGHTNESS, TRIM, COMPRESS, ENCODER, NONE
}

public class Selecting extends Activity {
    private Button showMediaInfoBtn;
    private Button playVideoBtn;
    private Button encodeWithOtherEncoderBtn;
    private Button encodeWithCompressBtn;
    private Button encodeWithTrimBtn;
    private Button encodeWithBrightnessBtn;
    private Button encodeWithMetadataBtn;

    // 外壳控件（在主selecting.xml，可以onCreate直接find）
    private View parametersWindow;
    private ImageButton backArrow;
    private Button btnConfirmEncode;
    private FrameLayout contentPlaceholder;
    private TextView tvParametersTitle;

    // 状态变量，保存选择的编码配置
    private String selectedVideoCodec = "";
    private String selectedAudioCodec = "";

    // 流信息（由 native hasVideo()/hasAudio() 提供）
    private boolean fileHasVideo = false;
    private boolean fileHasAudio = false;

    // 保存当前已经加载的子view，供确认按钮读取控件
    private View currentSubView = null;

    // 目前使用的函数窗口类型
    FunctionWindow window = FunctionWindow.NONE;

    // 输入文件路径（由 MainActivity 通过 Intent 传入）
    private String inputFilePath = "";

    // 参数窗口动画状态，防止动画期间重复触发
    private boolean isParamAnimating = false;

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        setContentView(R.layout.selecting);

        // 接收 MainActivity 传入的输入文件路径
        inputFilePath = getIntent().getStringExtra("InputPath");
        if (inputFilePath == null) inputFilePath = "";

        showMediaInfoBtn = <Button>findViewById(R.id.ShowMediaInfo);
        playVideoBtn = <Button>findViewById(R.id.PlayVideo);
        encodeWithOtherEncoderBtn = <Button>findViewById(R.id.EncodeWithOtherEncoders);
        encodeWithCompressBtn = <Button>findViewById(R.id.EncodeWithCompress);
        encodeWithTrimBtn = <Button>findViewById(R.id.EncodeWithTrim);
        encodeWithBrightnessBtn = <Button>findViewById(R.id.EncodeWithBrightness);
        encodeWithMetadataBtn = <Button>findViewById(R.id.EncodeWithMetaData);


        // 外壳控件
        parametersWindow = <View>findViewById(R.id.parameters_window);
        backArrow = <ImageButton>findViewById(R.id.backArrow);
        btnConfirmEncode = <Button>findViewById(R.id.btn_confirm_encode);
        contentPlaceholder = <FrameLayout>findViewById(R.id.content_placeholder);
        tvParametersTitle = <TextView>findViewById(R.id.tv_parameters_title);

        // 原生获取流信息
        fileHasVideo = hasVideo();
        fileHasAudio = hasAudio();

        // 实验功能开关(此版本不存在)
        // ......

        if (!hasVideo()) {
            encodeWithBrightnessBtn.setVisibility(View.GONE);
        }

        showMediaInfoBtn.setOnClickListener(v -> {
            Intent res = new Intent();
            res.putExtra("Function", "GetMediaInfo");
            setResult(RESULT_OK, res);
            finish();
        });

        playVideoBtn.setOnClickListener(v -> {
            Intent res = new Intent();
            res.putExtra("Function", "PlayVideo");
            setResult(RESULT_OK, res);
            finish();
        });

        // 【修改】调用通用方法加载编码器子布局，不再直接setVisible
        encodeWithOtherEncoderBtn.setOnClickListener(v -> {
                showParamWindow(R.layout.layout_encoder_params, getString(R.string.Selecting_parametersTitle));
                window = FunctionWindow.ENCODER;
            }
        );

        encodeWithCompressBtn.setOnClickListener(v -> {
                    showParamWindow(R.layout.layout_compress_params, getString(R.string.Selecting_parametersTitle));
                    window = FunctionWindow.COMPRESS;
                }
        );

        encodeWithTrimBtn.setOnClickListener(v -> {
                showParamWindow(R.layout.layout_trim_params, getString(R.string.Selecting_parametersTitle));
                window = FunctionWindow.TRIM;
            }
        );

        encodeWithBrightnessBtn.setOnClickListener(v -> {
                showParamWindow(R.layout.layout_brightness_params, getString(R.string.Selecting_parametersTitle));
                window = FunctionWindow.BRIGHTNESS;
                // brightness 输入框限制 -128~127
                EditText etBrightness = currentSubView.<EditText>findViewById(R.id.et_brightness);
                etBrightness.setFilters(new android.text.InputFilter[]{ (source, start, end, dest, dstart, dend) -> {
                    String newVal = dest.subSequence(0, dstart) + source.toString() + dest.subSequence(dend, dest.length());
                    if (newVal.isEmpty() || newVal.equals("-")) return null; // 允许中间状态
                    try {
                        int val = Integer.parseInt(newVal);
                        return (val >= -128 && val <= 127) ? null : "";
                    } catch (NumberFormatException e) {
                        return "";
                    }
                }});
            }
        );

        encodeWithMetadataBtn.setOnClickListener(v -> {
                showParamWindow(R.layout.layout_metadata_params, getString(R.string.Selecting_parametersTitle));
                window = FunctionWindow.METADATA;
            }
        );

        // 返回箭头
        backArrow.setOnClickListener(v -> {
            if (parametersWindow.getVisibility() == View.VISIBLE) {
                hideParamWindow();
            } else {
                setResult(RESULT_CANCELED);
                finish();
            }
        });

        // 确认编码按钮
        btnConfirmEncode.setOnClickListener(v -> {
            if (window == FunctionWindow.ENCODER) {
                if (currentSubView == null) return;

                // 从 currentSubView 查找输入框
                EditText etOutputPath = currentSubView.<EditText>findViewById(R.id.et_output_path);
                String outputPath = etOutputPath.getText().toString().trim();

                if (outputPath.isEmpty()) {
                    Toast.makeText(this, R.string.Selecting_toast_emptyOutputPath, Toast.LENGTH_SHORT).show();
                    return;
                }
                if (selectedVideoCodec.isEmpty() && selectedAudioCodec.isEmpty()) {
                    Toast.makeText(this, R.string.Selecting_toast_noStream, Toast.LENGTH_SHORT).show();
                    return;
                }

                Intent res = new Intent();
                res.putExtra("Function", "EncodeWithOtherEncoders");
                res.putExtra("OutputPath", outputPath);
                res.putExtra("VideoCodec", selectedVideoCodec);
                res.putExtra("AudioCodec", selectedAudioCodec);
                setResult(RESULT_OK, res);
                finish();
            }
            if (window == FunctionWindow.COMPRESS) {
                if (currentSubView == null) return;

                // 从 currentSubView 查找输入框
                EditText etOutputPath = currentSubView.<EditText>findViewById(R.id.et_output_path);
                String outputPath = etOutputPath.getText().toString().trim();

                if (outputPath.isEmpty()) {
                    Toast.makeText(this, R.string.Selecting_toast_emptyOutputPath, Toast.LENGTH_SHORT).show();
                    return;
                }

                EditText et_compressLevel = currentSubView.<EditText>findViewById(R.id.compressLevel);
                String compressLevel = et_compressLevel.getText().toString().trim();
                if ((compressLevel.length() != 1 && (compressLevel.length() == 2 && !compressLevel.equals("10"))) || compressLevel.isEmpty() ||
                        !('0' <= compressLevel.charAt(0) || compressLevel.charAt(0) <= '9') ) {
                    Toast.makeText(this, R.string.Selecting_toast_invalidCompressLevel, Toast.LENGTH_SHORT).show();
                    return;
                }
                Intent res = new Intent();
                res.putExtra("Function", "EncodeWithCompress");
                res.putExtra("OutputPath", outputPath);
                res.putExtra("CompressLevel", compressLevel);
                setResult(RESULT_OK, res);
                finish();
            }
            if (window == FunctionWindow.TRIM) {
                if (currentSubView == null) return;
                EditText etOutputPath = currentSubView.<EditText>findViewById(R.id.et_output_path);
                String outputPath = etOutputPath.getText().toString().trim();

                if (outputPath.isEmpty()) {
                    Toast.makeText(this, R.string.Selecting_toast_emptyOutputPath, Toast.LENGTH_SHORT).show();
                    return;
                }

                int AV_TIME_BASE = 1000000; // C++ AV_TIME_BASE兼容

                EditText etStartTime = currentSubView.<EditText>findViewById(R.id.startTimeSecond);
                double startTime = Double.parseDouble(etStartTime.getText().toString().trim());

                // duration 未知（=0）时跳过上限检查，避免合法时间被误判为非法
                double durationSec = (double)durationUs() / AV_TIME_BASE;
                if (startTime < 0 || (durationSec > 0 && startTime > durationSec)) {
                    Toast.makeText(this, R.string.Selecting_toast_invalidStartTime, Toast.LENGTH_SHORT).show();
                    return;
                }

                EditText etEndTime = currentSubView.<EditText>findViewById(R.id.endTimeSecond);
                double endTime = Double.parseDouble(etEndTime.getText().toString().trim());
                if (endTime <= startTime) {
                    Toast.makeText(this, R.string.Selecting_toast_invalidEndTime, Toast.LENGTH_SHORT).show();
                    return;
                }

                Intent res = new Intent();
                res.putExtra("Function", "EncodeWithTrim");
                res.putExtra("OutputPath", outputPath);
                res.putExtra("StartTime", startTime);
                res.putExtra("EndTime", endTime);
                setResult(RESULT_OK, res);
                finish();
            }
            if (window == FunctionWindow.BRIGHTNESS) {
                if (currentSubView == null) return;
                EditText etOutputPath = currentSubView.<EditText>findViewById(R.id.et_output_path);
                String outputPath = etOutputPath.getText().toString().trim();

                if (outputPath.isEmpty()) {
                    Toast.makeText(this, R.string.Selecting_toast_emptyOutputPath, Toast.LENGTH_SHORT).show();
                    return;
                }

                if (!hasVideo()) {
                    Toast.makeText(this, R.string.Selecting_toast_noVideoStream, Toast.LENGTH_SHORT).show();
                    return;
                }

                EditText etBrightness = currentSubView.<EditText>findViewById(R.id.et_brightness);
                int brightness = Integer.parseInt(etBrightness.getText().toString().trim());
                if (-128 > brightness || 127 < brightness) {
                    Toast.makeText(this, R.string.Selecting_toast_invalidBrightness, Toast.LENGTH_SHORT).show();
                    return;
                }

                EditText etGamma = currentSubView.<EditText>findViewById(R.id.et_gamma);
                String gammaStr = etGamma.getText().toString().trim();
                float gamma = gammaStr.isEmpty() ? 1.0f : Float.parseFloat(gammaStr);

                if (gamma < 0.5f || 3.0f < gamma) {
                    Toast.makeText(this, R.string.Selecting_toast_invalidGamma, Toast.LENGTH_SHORT).show();
                    return;
                }

                Intent res = new Intent();
                res.putExtra("Function", "EncodeWithBrightness");
                res.putExtra("OutputPath", outputPath);
                res.putExtra("Brightness", brightness);
                res.putExtra("Gamma", gamma);
                setResult(RESULT_OK, res);
                finish();
            }
            if (window == FunctionWindow.METADATA) {
                if (currentSubView == null) return;
                EditText etOutputPath = currentSubView.<EditText>findViewById(R.id.et_output_path);
                String outputPath = etOutputPath.getText().toString().trim();
                if (outputPath.isEmpty()) {
                    Toast.makeText(this, R.string.Selecting_toast_emptyOutputPath, Toast.LENGTH_SHORT).show();
                    return;
                }

                EditText etTitle = currentSubView.<EditText>findViewById(R.id.et_title);
                EditText etAlbum = currentSubView.<EditText>findViewById(R.id.et_album);
                EditText etArtist = currentSubView.<EditText>findViewById(R.id.et_artist);
                EditText etDate = currentSubView.<EditText>findViewById(R.id.et_date);
                EditText etLanguage = currentSubView.<EditText>findViewById(R.id.et_language);
                EditText etAlbumArtist = currentSubView.<EditText>findViewById(R.id.et_albumArtist);
                EditText etGenre = currentSubView.<EditText>findViewById(R.id.et_genre);

                Intent res = new Intent();
                res.putExtra("Function", "EncodeWithMetadata");
                res.putExtra("OutputPath", outputPath);
                res.putExtra("Title", etTitle.getText().toString().trim());
                res.putExtra("Album", etAlbum.getText().toString().trim());
                res.putExtra("Artist", etArtist.getText().toString().trim());
                res.putExtra("Date", etDate.getText().toString().trim());
                res.putExtra("Language", etLanguage.getText().toString().trim());
                res.putExtra("AlbumArtist", etAlbumArtist.getText().toString().trim());
                res.putExtra("Genre", etGenre.getText().toString().trim());
                setResult(RESULT_OK, res);
                finish();
            }
        });
    }

    /**
     * 通用加载参数窗口
     */
    private void showParamWindow(int layoutId, String titleStr) {
        if (isParamAnimating) return;
        contentPlaceholder.removeAllViews();
        currentSubView = LayoutInflater.from(this).inflate(layoutId, contentPlaceholder, false);
        contentPlaceholder.addView(currentSubView);
        tvParametersTitle.setText(titleStr);

        // 如果打开的是编码器参数布局，在这里初始化spinner
        if (layoutId == R.layout.layout_encoder_params) {
            initEncoderSubView(currentSubView);
        }

        // 自动填充输出路径：/Download/<输入文件名>
        EditText etOutput = currentSubView.<EditText>findViewById(R.id.et_output_path);
        if (etOutput != null && !inputFilePath.isEmpty()) {
            String fileName = inputFilePath.substring(inputFilePath.lastIndexOf('/') + 1);
            String downloadDir = android.os.Environment.getExternalStoragePublicDirectory(
                    android.os.Environment.DIRECTORY_DOWNLOADS).getAbsolutePath();
            etOutput.setText(downloadDir + "/" + fileName);
        }

        // 元数据布局：回车收起软键盘（不确认编码）
        if (layoutId == R.layout.layout_metadata_params) {
            attachEnterToHideKeyboard(currentSubView);
        }

        // 入场动画：上滑 + 轻微缩放 + 淡入
        isParamAnimating = true;
        float dip = getResources().getDisplayMetrics().density;
        parametersWindow.clearAnimation();
        parametersWindow.setVisibility(View.VISIBLE);
        parametersWindow.setTranslationY(60f * dip);   // 从下方 60dp 开始
        parametersWindow.setScaleX(0.92f);
        parametersWindow.setScaleY(0.92f);
        parametersWindow.setAlpha(0f);
        parametersWindow.animate()
                .translationY(0f)
                .scaleX(1f)
                .scaleY(1f)
                .alpha(1f)
                .setDuration(280)
                .setInterpolator(new DecelerateInterpolator(1.5f))
                .withEndAction(() -> isParamAnimating = false)
                .start();
    }

    /**
     * 递归遍历 View 树，给所有 EditText 挂回车：收起软键盘（不确认编码）
     */
    private void attachEnterToHideKeyboard(View view) {
        if (view instanceof EditText) {
            EditText et = (EditText) view;
            et.setOnEditorActionListener((v, actionId, event) -> {
                if (actionId == EditorInfo.IME_ACTION_DONE ||
                    actionId == EditorInfo.IME_ACTION_NEXT ||
                    actionId == EditorInfo.IME_ACTION_GO ||
                    actionId == EditorInfo.IME_ACTION_SEND ||
                    actionId == EditorInfo.IME_ACTION_UNSPECIFIED) {
                    hideKeyboard(et);
                    return true;
                }
                return false;
            });
            et.setOnKeyListener((v, keyCode, event) -> {
                if (keyCode == android.view.KeyEvent.KEYCODE_ENTER
                        && event.getAction() == android.view.KeyEvent.ACTION_UP) {
                    hideKeyboard(et);
                    return true;
                }
                return false;
            });
        } else if (view instanceof ViewGroup) {
            ViewGroup group = (ViewGroup) view;
            for (int i = 0; i < group.getChildCount(); i++) {
                attachEnterToHideKeyboard(group.getChildAt(i));
            }
        }
    }

    /** 收起软键盘并清除焦点 */
    private void hideKeyboard(View view) {
        android.view.inputmethod.InputMethodManager imm =
            (android.view.inputmethod.InputMethodManager) getSystemService(Activity.INPUT_METHOD_SERVICE);
        if (imm != null) {
            imm.hideSoftInputFromWindow(view.getWindowToken(), 0);
        }
        view.clearFocus();
    }

    /**
     * 退场动画（下滑 + 轻微缩放 + 淡出），供返回箭头 / 返回键共用
     */
    private void hideParamWindow() {
        if (isParamAnimating || parametersWindow.getVisibility() != View.VISIBLE) return;
        isParamAnimating = true;
        float dip = getResources().getDisplayMetrics().density;
        parametersWindow.clearAnimation();
        parametersWindow.animate()
                .translationY(50f * dip)
                .scaleX(0.96f)
                .scaleY(0.96f)
                .alpha(0f)
                .setDuration(200)
                .setInterpolator(new AccelerateInterpolator(1.5f))
                .withEndAction(() -> {
                    parametersWindow.setVisibility(View.GONE);
                    // 复位变换，避免下次入场时继承残留状态
                    parametersWindow.setTranslationY(0f);
                    parametersWindow.setScaleX(1f);
                    parametersWindow.setScaleY(1f);
                    parametersWindow.setAlpha(1f);
                    window = FunctionWindow.NONE;
                    isParamAnimating = false;
                })
                .start();
    }

    /**
     * 初始化编码器子布局内的控件（spinner、显示隐藏分组）
     */
    private void initEncoderSubView(View subView) {
        LinearLayout videoCodecGroup = subView.<LinearLayout>findViewById(R.id.video_codec_group);
        Spinner spinnerVideoCodec = subView.<Spinner>findViewById(R.id.spinner_video_codec);
        LinearLayout audioCodecGroup = subView.<LinearLayout>findViewById(R.id.audio_codec_group);
        Spinner spinnerAudioCodec = subView.<Spinner>findViewById(R.id.spinner_audio_codec);

        // 视频编码器
        if (fileHasVideo) {
            videoCodecGroup.setVisibility(View.VISIBLE);
            final String[] videoNames = {"libx264", "libx265", "mpeg4", "mpeg2video"};
            String[] videoLabels = {
                    "H.264 (AVC)",
                    "H.265 (HEVC)",
                    "MPEG-4",
                    "MPEG-2"
            };
            ArrayAdapter<String> vAdapter = new ArrayAdapter<>(
                    this, R.layout.spinner_item, videoLabels);
            vAdapter.setDropDownViewResource(R.layout.spinner_drop_item);
            spinnerVideoCodec.setAdapter(vAdapter);
            spinnerVideoCodec.setSelection(0);
            selectedVideoCodec = videoNames[0];

            spinnerVideoCodec.setOnItemSelectedListener(new AdapterView.OnItemSelectedListener() {
                @Override
                public void onItemSelected(AdapterView<?> parent, View view, int position, long id) {
                    selectedVideoCodec = videoNames[position];
                }
                @Override
                public void onNothingSelected(AdapterView<?> parent) {
                    selectedVideoCodec = videoNames[0];
                }
            });
        } else {
            videoCodecGroup.setVisibility(View.GONE);
            selectedVideoCodec = "";
        }

        // 音频编码器
        if (fileHasAudio) {
            audioCodecGroup.setVisibility(View.VISIBLE);
            final String[] audioNames = {"aac", "libmp3lame", "pcm_s16le", "flac", "opus",
                    "pcm_s24le", "vorbis"};
            String[] audioLabels = {
                    "AAC (aac)",
                    "MP3 (libmp3lame)",
                    "PCM 16bit (pcm_s16le)",
                    "FLAC (flac)",
                    "OPUS",
                    "PCM 24bit (pcm_s24le)",
                    "VORBIS"
            };
            ArrayAdapter<String> aAdapter = new ArrayAdapter<>(
                    this, R.layout.spinner_item, audioLabels);
            aAdapter.setDropDownViewResource(R.layout.spinner_drop_item);
            spinnerAudioCodec.setAdapter(aAdapter);
            spinnerAudioCodec.setSelection(0);
            selectedAudioCodec = audioNames[0];

            spinnerAudioCodec.setOnItemSelectedListener(new AdapterView.OnItemSelectedListener() {
                @Override
                public void onItemSelected(AdapterView<?> parent, View view, int position, long id) {
                    selectedAudioCodec = audioNames[position];
                }
                @Override
                public void onNothingSelected(AdapterView<?> parent) {
                    selectedAudioCodec = audioNames[0];
                }
            });
        } else {
            audioCodecGroup.setVisibility(View.GONE);
            selectedAudioCodec = "";
        }
    }

    @Override
    public void onBackPressed() {
        if (parametersWindow.getVisibility() == View.VISIBLE) {
            hideParamWindow();
        } else {
            super.onBackPressed();
        }
    }

    public native String getInputFileName();
    public native boolean hasVideo();
    public native boolean hasAudio();
    public native long durationUs();
    public native boolean checkEnableExperimentalFunction();
}
