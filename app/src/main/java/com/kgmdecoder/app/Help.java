package com.kgmdecoder.app;

import android.app.Activity;
import android.os.Bundle;
import android.widget.TextView;
import android.widget.ImageButton;

public class Help extends Activity {
    private ImageButton back;
    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        setContentView(R.layout.help);
        back = findViewById(R.id.btnBack);
        back.setOnClickListener(v -> {
            finish();
        });
    }
}
