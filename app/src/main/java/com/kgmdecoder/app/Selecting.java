package com.kgmdecoder.app;

import android.app.Activity;
import android.content.Intent;
import android.os.Bundle;
import android.view.LayoutInflater;
import android.view.View;
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
    COMPRESS, ENCODER, NONE
}

public class Selecting extends Activity {
    private Button showMediaInfoBtn;
    private Button playVideoBtn;
    private Button encodeWithOtherEncoderBtn;
    private Button encodeWithCompressBtn;

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

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        setContentView(R.layout.selecting);

        showMediaInfoBtn = findViewById(R.id.ShowMediaInfo);
        playVideoBtn = findViewById(R.id.PlayVideo);
        encodeWithOtherEncoderBtn = findViewById(R.id.EncodeWithOtherEncoders);
        encodeWithCompressBtn = findViewById(R.id.EncodeWithCompress);

        // 外壳控件
        parametersWindow = findViewById(R.id.parameters_window);
        backArrow = findViewById(R.id.backArrow);
        btnConfirmEncode = findViewById(R.id.btn_confirm_encode);
        contentPlaceholder = findViewById(R.id.content_placeholder);
        tvParametersTitle = findViewById(R.id.tv_parameters_title);

        // 原生获取流信息
        fileHasVideo = hasVideo();
        fileHasAudio = hasAudio();

        // 实验功能开关(此版本不存在)
        // ......


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
                    showParamWindow(R.layout.layout_compress_params, "压缩参数");
                    window = FunctionWindow.COMPRESS;
                }
        );

        // 返回箭头
        backArrow.setOnClickListener(v -> {
            if (parametersWindow.getVisibility() == View.VISIBLE) {
                parametersWindow.setVisibility(View.GONE);
                window = FunctionWindow.NONE;
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
                EditText etOutputPath = currentSubView.findViewById(R.id.et_output_path);
                String outputPath = etOutputPath.getText().toString().trim();

                if (outputPath.isEmpty()) {
                    Toast.makeText(this, "请输入输出路径", Toast.LENGTH_SHORT).show();
                    return;
                }
                if (selectedVideoCodec.isEmpty() && selectedAudioCodec.isEmpty()) {
                    Toast.makeText(this, "文件不含可编码的音视频流", Toast.LENGTH_SHORT).show();
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
                EditText etOutputPath = currentSubView.findViewById(R.id.et_output_path);
                String outputPath = etOutputPath.getText().toString().trim();

                if (outputPath.isEmpty()) {
                    Toast.makeText(this, "请输入输出路径", Toast.LENGTH_SHORT).show();
                    return;
                }

                EditText et_compressLevel = currentSubView.findViewById(R.id.compressLevel);
                String compressLevel = et_compressLevel.getText().toString().trim();
                if ((compressLevel.length() != 1 && (compressLevel.length() == 2 && !compressLevel.equals("10"))) || compressLevel.isEmpty() ||
                        !('0' <= compressLevel.charAt(0) || compressLevel.charAt(0) <= '9') ) {
                    Toast.makeText(this, "请指定1~10(包括1和10)的数字", Toast.LENGTH_SHORT).show();
                    return;
                }
                Intent res = new Intent();
                res.putExtra("Function", "EncodeWithCompress");
                res.putExtra("OutputPath", outputPath);
                res.putExtra("CompressLevel", compressLevel);
                setResult(RESULT_OK, res);
                finish();
            }
        });
    }

    /**
     * 通用加载参数窗口
     */
    private void showParamWindow(int layoutId, String titleStr) {
        contentPlaceholder.removeAllViews();
        currentSubView = LayoutInflater.from(this).inflate(layoutId, contentPlaceholder, false);
        contentPlaceholder.addView(currentSubView);
        tvParametersTitle.setText(titleStr);

        // 如果打开的是编码器参数布局，在这里初始化spinner
        if (layoutId == R.layout.layout_encoder_params) {
            initEncoderSubView(currentSubView);
        }

        parametersWindow.setVisibility(View.VISIBLE);
    }

    /**
     * 初始化编码器子布局内的控件（spinner、显示隐藏分组）
     */
    private void initEncoderSubView(View subView) {
        LinearLayout videoCodecGroup = subView.findViewById(R.id.video_codec_group);
        Spinner spinnerVideoCodec = subView.findViewById(R.id.spinner_video_codec);
        LinearLayout audioCodecGroup = subView.findViewById(R.id.audio_codec_group);
        Spinner spinnerAudioCodec = subView.findViewById(R.id.spinner_audio_codec);

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
            parametersWindow.setVisibility(View.GONE);
        } else {
            super.onBackPressed();
        }
    }

    public native boolean hasVideo();
    public native boolean hasAudio();
    public native boolean checkEnableExperimentalFunction();
}
