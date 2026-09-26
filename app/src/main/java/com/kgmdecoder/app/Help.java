package com.kgmdecoder.app;

import android.annotation.SuppressLint;
import android.app.Activity;
import android.content.Context;
import android.os.Bundle;
import android.view.inputmethod.EditorInfo;
import android.view.inputmethod.InputMethodManager;
import android.widget.EditText;
import android.widget.ImageButton;
import android.widget.Switch;
import android.widget.Toast;

public class Help extends Activity {
    private ImageButton back;
    private Switch experimentalFunction;
    private EditText et_encodeThreads;

    // 配置文件路径，统一常量，避免硬编码重复
    private static final String CONFIG_PATH = "/data/user/0/com.kgmdecoder.app/files/config.dat";

    @SuppressLint("SetTextI18n")
    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        setContentView(R.layout.help);
        back = findViewById(R.id.btnBack);
        et_encodeThreads = findViewById(R.id.et_encodeThreads);
        experimentalFunction = findViewById(R.id.switch_experimentalFunction);

        // 读取native配置，初始化UI状态
        experimentalFunction.setChecked(checkEnableExperimentalFunction());
        int threads = getEncodeThreads();
        et_encodeThreads.setText(String.valueOf(threads));

        back.setOnClickListener(v -> finish());

        // 开关监听，优先使用 setOnCheckedChangeListener
        experimentalFunction.setOnCheckedChangeListener((buttonView, isChecked) -> {
            if (isChecked) {
                writeLine(CONFIG_PATH, 1, "1");
            } else {
                writeLine(CONFIG_PATH, 1, "0");
            }
        });

        // EditText：单行输入，按下完成键确认保存线程数
        et_encodeThreads.setOnEditorActionListener((v, actionId, event) -> {
            if (actionId == EditorInfo.IME_ACTION_DONE) {
                // 读取输入框文本
                String inputText = et_encodeThreads.getText().toString().trim();

                // 写入配置文件第2行
                writeLine(CONFIG_PATH, 2, inputText);
                if (getEncodeThreads() == -1) {
                    writeLine(CONFIG_PATH, 2, "2");
                    Toast.makeText(this, "请写入一个合法的值(否则为2)", Toast.LENGTH_SHORT).show();
                    et_encodeThreads.setText("2");
                }
                if (getEncodeThreads() == -2) {
                    writeLine(CONFIG_PATH, 2, String.valueOf(Runtime.getRuntime().availableProcessors()));
                }

                // 收起软键盘
                InputMethodManager imm = (InputMethodManager) getSystemService(Context.INPUT_METHOD_SERVICE);
                imm.hideSoftInputFromWindow(et_encodeThreads.getWindowToken(), 0);

                return true; // 消费事件
            }
            return false;
        });
    }

    // native 本地方法声明
    public native boolean checkEnableExperimentalFunction();
    public native boolean writeLine(String path, int lineIndex, String text);
    public native int getEncodeThreads();

    static {
        System.loadLibrary("native");
    }
}
