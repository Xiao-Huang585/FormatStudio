package com.kgmdecoder.app;

import android.annotation.SuppressLint;
import android.app.Activity;
import android.content.Intent;
import android.widget.Button;
import android.os.Bundle;
import android.widget.TextView;
import android.widget.ImageButton;

public class Selecting extends Activity {

    private Button showMediaInfoBtn;
    private Button playVideoBtn;
    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        setContentView(R.layout.selecting);
        showMediaInfoBtn = findViewById(R.id.ShowMediaInfo);
        playVideoBtn = findViewById(R.id.PlayVideo);

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
    }
}
