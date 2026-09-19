package com.kgmdecoder.app;

import android.annotation.SuppressLint;
import android.app.Activity;
import android.content.Context;
import android.os.Bundle;
import android.widget.ImageButton;
import android.widget.Switch;

public class Help extends Activity {
    private ImageButton back;
    private Switch experimentalFunction;
    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        setContentView(R.layout.help);
        back = findViewById(R.id.btnBack);
        experimentalFunction = findViewById(R.id.switch_experimentalFunction);
        experimentalFunction.setChecked(checkEnableExperimentalFunction());
        back.setOnClickListener(v -> {
            finish();
        });
        experimentalFunction.setOnClickListener(v -> {
            if (checkEnableExperimentalFunction()) {
                writeLine("/data/user/0/com.kgmdecoder.app/files/config.dat", 1, "0");
            } else {
                writeLine("/data/user/0/com.kgmdecoder.app/files/config.dat", 1, "1");
            }
        });
    }

    public native boolean checkEnableExperimentalFunction();

    public native boolean writeLine(String path, int lineIndex, String text);
}
