package com.kgmdecoder.app;

import android.annotation.SuppressLint;
import android.app.Activity;
import android.content.Intent;
import android.os.Bundle;
import android.view.View;
import android.widget.AdapterView;
import android.widget.ArrayAdapter;
import android.widget.Button;
import android.widget.EditText;
import android.widget.ImageButton;
import android.widget.LinearLayout;
import android.widget.Spinner;
import android.widget.TextView;
import android.widget.Toast;

public class Selecting extends Activity {

    private Button showMediaInfoBtn;
    private Button playVideoBtn;
    private Button encodeWithOtherEncoderBtn;

    // 参数窗口控件
    private View parametersWindow;
    private ImageButton backArrow;
    private EditText etOutputPath;
    private Button btnConfirmEncode;

    // 视频编码器
    private LinearLayout videoCodecGroup;
    private Spinner spinnerVideoCodec;
    private String selectedVideoCodec = "";

    // 音频编码器
    private LinearLayout audioCodecGroup;
    private Spinner spinnerAudioCodec;
    private String selectedAudioCodec = "";

    // 流信息（由 native hasVideo()/hasAudio() 提供）
    private boolean fileHasVideo = false;
    private boolean fileHasAudio = false;

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        setContentView(R.layout.selecting);
        showMediaInfoBtn = findViewById(R.id.ShowMediaInfo);
        playVideoBtn = findViewById(R.id.PlayVideo);
        encodeWithOtherEncoderBtn = findViewById(R.id.EncodeWithOtherEncoders);

        parametersWindow = findViewById(R.id.parameters_window);
        backArrow = findViewById(R.id.backArrow);
        etOutputPath = findViewById(R.id.et_output_path);
        btnConfirmEncode = findViewById(R.id.btn_confirm_encode);

        // 视频编码器标签 + 下拉菜单放在同一容器，方便整体显示/隐藏
        videoCodecGroup = findViewById(R.id.video_codec_group);
        spinnerVideoCodec = findViewById(R.id.spinner_video_codec);

        // 音频编码器标签 + 下拉菜单放在同一容器
        audioCodecGroup = findViewById(R.id.audio_codec_group);
        spinnerAudioCodec = findViewById(R.id.spinner_audio_codec);

        // ====== 查询文件流信息：决定显示视频/音频编码器预设 ======
        fileHasVideo = hasVideo();
        fileHasAudio = hasAudio();

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

        // 显示编码器参数窗口
        encodeWithOtherEncoderBtn.setOnClickListener(v -> {
            parametersWindow.setVisibility(View.VISIBLE);
        });

        // 返回按钮：参数窗口可见时先隐藏，否则结束 Activity
        backArrow.setOnClickListener(v -> {
            if (parametersWindow.getVisibility() == View.VISIBLE) {
                parametersWindow.setVisibility(View.GONE);
            } else {
                setResult(RESULT_CANCELED);
                finish();
            }
        });

        // ====== 配置视频编码器下拉菜单（仅当文件含视频流） ======
        if (fileHasVideo) {
            videoCodecGroup.setVisibility(View.VISIBLE);
            // 编码器显示名 → FFmpeg 编码器名
            final String[] videoNames = {"libx264", "libx265", "mpeg4"};
            String[] videoLabels = {
                    "H.264 (libx264)",
                    "H.265 (libx265)",
                    "MPEG-4 (mpeg4)"
            };
            ArrayAdapter<String> vAdapter = new ArrayAdapter<>(
                    this, android.R.layout.simple_spinner_item, videoLabels);
            vAdapter.setDropDownViewResource(android.R.layout.simple_spinner_dropdown_item);
            spinnerVideoCodec.setAdapter(vAdapter);
            spinnerVideoCodec.setSelection(0); // 默认 H.264
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
            // 没有视频流：隐藏视频编码器选择，不输出视频
            videoCodecGroup.setVisibility(View.GONE);
            selectedVideoCodec = "";
        }

        // ====== 配置音频编码器下拉菜单（仅当文件含音频流） ======
        if (fileHasAudio) {
            audioCodecGroup.setVisibility(View.VISIBLE);
            final String[] audioNames = {"aac", "libmp3lame", "pcm_s16le", "flac"};
            String[] audioLabels = {
                    "AAC (aac)",
                    "MP3 (libmp3lame)",
                    "PCM 16bit (pcm_s16le)",
                    "FLAC 无损 (flac)"
            };
            ArrayAdapter<String> aAdapter = new ArrayAdapter<>(
                    this, android.R.layout.simple_spinner_item, audioLabels);
            aAdapter.setDropDownViewResource(android.R.layout.simple_spinner_dropdown_item);
            spinnerAudioCodec.setAdapter(aAdapter);
            spinnerAudioCodec.setSelection(0); // 默认 AAC
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
            // 没有音频流：隐藏音频编码器选择，不输出音频
            audioCodecGroup.setVisibility(View.GONE);
            selectedAudioCodec = "";
        }

        // ====== 确认编码：校验参数并回传给 MainActivity ======
        btnConfirmEncode.setOnClickListener(v -> {
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
        });
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
}
