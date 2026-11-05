package com.kgmdecoder.app;

import android.app.Activity;
import android.content.Context;
import android.content.ContextWrapper;
import android.content.Intent;
import android.net.Uri;
import android.os.Environment;
import android.provider.DocumentsContract;
import android.util.AttributeSet;
import android.widget.EditText;
import android.widget.Toast;

import androidx.activity.ComponentActivity;
import androidx.activity.result.ActivityResultLauncher;
import androidx.activity.result.contract.ActivityResultContracts;

import com.google.android.material.button.MaterialButton;

import java.io.File;

/**
 * 文件选择按钮。
 *
 * 继承自 {@link com.google.android.material.button.MaterialButton}，android:text / android:textColor 等标准属性在预览和真机上都直接生效。
 * 点击后呼出系统原生的文件选择界面（SAF，ACTION_OPEN_DOCUMENT），选中后返回文件绝对路径。
 *
 * <pre>
 * FilePickerButton picker = findViewById(R.id.picker);
 * picker.setMimeType("video/*");                       // 可选，默认 "*&#47;*"
 * picker.setOnFileSelectedListener((path, uri) -> {    // path 形如 /storage/emulated/0/Download/a.mp4
 *     Log.d(TAG, "选中: " + path);
 * });
 * </pre>
 *
 * 注意：所在 Activity 需为 androidx 的 ComponentActivity（或 AppCompatActivity），
 * 这样组件可以自行注册结果回调，接入方无需重写 onActivityResult。
 */
public class FilePickerButton extends MaterialButton {

    /** 选中文件回调：absolutePath 为解析出的绝对路径（无法解析时为 null），uri 为系统返回的 Uri */
    public interface OnFileSelectedListener {
        void onFileSelected(String absolutePath, Uri uri);
    }

    private String mimeType = "*/*";
    private String selectedPath;
    private Uri selectedUri;
    private OnFileSelectedListener selectedListener;
    private EditText targetEditText;
    private ActivityResultLauncher<Intent> launcher;

    public FilePickerButton(Context context) {
        this(context, null);
    }

    public FilePickerButton(Context context, AttributeSet attrs) {
        this(context, attrs, com.google.android.material.R.attr.materialButtonStyle);
    }

    public FilePickerButton(Context context, AttributeSet attrs, int defStyleAttr) {
        super(context, attrs, defStyleAttr);
        setAllCaps(false);
        super.setOnClickListener(v -> showPicker());
    }

    /** 限定可选文件类型，如 "video/*"、"audio/*"、".mp4"；默认 "*&#47;*" */
    public void setMimeType(String mimeType) {
        if (mimeType != null && !mimeType.isEmpty()) this.mimeType = mimeType;
    }

    /** 上次选中文件的绝对路径，未选择或无法解析时为 null */
    public String getSelectedPath() {
        return selectedPath;
    }

    /** 上次选中文件的 Uri，未选择时为 null */
    public Uri getSelectedUri() {
        return selectedUri;
    }

    /** 选中文件后的回调 */
    public void setOnFileSelectedListener(OnFileSelectedListener listener) {
        this.selectedListener = listener;
    }

    /**
     * 绑定一个 EditText：选中文件后自动把路径填入该控件，无需再写回调。
     * 无法还原绝对路径时（第三方 provider）填入 Uri 字符串。
     */
    public void setTargetEditText(EditText editText) {
        this.targetEditText = editText;
    }

    private void showPicker() {
        Context context = getContext();
        ComponentActivity activity = findComponentActivity(context);
        if (activity == null) {
            Toast.makeText(context,
                    "FilePickerButton 需要 androidx 的 ComponentActivity",
                    Toast.LENGTH_SHORT).show();
            return;
        }

        if (launcher == null) {
            String key = "FilePickerButton:" + System.identityHashCode(this);
            launcher = activity.getActivityResultRegistry().register(key,
                    new ActivityResultContracts.StartActivityForResult(),
                    result -> {
                        if (result.getResultCode() != Activity.RESULT_OK) return;
                        Intent data = result.getData();
                        if (data == null || data.getData() == null) return;

                        Uri uri = data.getData();
                        selectedUri = uri;
                        selectedPath = resolvePath(context, uri);
                        if (targetEditText != null) {
                            targetEditText.setText(selectedPath != null
                                    ? selectedPath
                                    : uri.toString());
                        }
                        if (selectedListener != null) {
                            selectedListener.onFileSelected(selectedPath, uri);
                        }
                    });
        }

        Intent intent = new Intent(Intent.ACTION_OPEN_DOCUMENT);
        intent.addCategory(Intent.CATEGORY_OPENABLE);
        intent.setType(mimeType);
        launcher.launch(intent);
    }

    private static ComponentActivity findComponentActivity(Context context) {
        while (context instanceof ContextWrapper) {
            if (context instanceof ComponentActivity) return (ComponentActivity) context;
            context = ((ContextWrapper) context).getBaseContext();
        }
        return null;
    }

    /** 把 SAF 返回的 content Uri 还原成 /storage/... 这样的绝对路径 */
    private static String resolvePath(Context context, Uri uri) {
        if (uri == null) return null;
        if ("file".equals(uri.getScheme())) return uri.getPath();

        try {
            if (DocumentsContract.isDocumentUri(context, uri)
                    && "com.android.externalstorage.documents".equals(uri.getAuthority())) {
                String docId = DocumentsContract.getDocumentId(uri);
                String[] parts = docId.split(":");
                String volume = parts[0];
                String relative = parts.length > 1 ? parts[1] : "";
                String root = "primary".equalsIgnoreCase(volume)
                        ? Environment.getExternalStorageDirectory().getAbsolutePath()
                        : "/storage/" + volume;
                return relative.isEmpty() ? root : root + "/" + relative;
            }
        } catch (Exception ignored) {
            // 非 externalstorage provider 或 docId 格式异常，无法还原为真实路径
        }
        return null;
    }

    /** 仅供需要直接判断路径的调用方使用 */
    public File getSelectedFile() {
        return selectedPath == null ? null : new File(selectedPath);
    }
}
