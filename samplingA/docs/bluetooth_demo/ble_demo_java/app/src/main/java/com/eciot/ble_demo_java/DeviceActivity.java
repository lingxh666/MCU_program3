package com.eciot.ble_demo_java;

import android.os.Build;
import android.os.Bundle;
import androidx.activity.EdgeToEdge;
import androidx.appcompat.app.AppCompatActivity;
import androidx.core.graphics.Insets;
import androidx.core.view.ViewCompat;
import androidx.core.view.WindowInsetsCompat;
import android.annotation.SuppressLint;
import android.util.Log;
import android.view.View;
import android.widget.CheckBox;
import android.widget.EditText;
import android.widget.RadioButton;
import android.widget.ScrollView;
import android.widget.TextView;
import android.widget.Toast;
import androidx.appcompat.app.AlertDialog;
import androidx.core.view.WindowInsetsControllerCompat;
import java.util.Date;
import java.util.regex.Pattern;
import java.text.SimpleDateFormat;

public class DeviceActivity extends AppCompatActivity {

    ScrollView scrollView = null;
    TextView receiveDataTextView = null;
    CheckBox scrollCheckBox = null;
    CheckBox hexRevCheckBox = null;
    CheckBox hexSendCheckBox = null;
    EditText sendDataEditText = null;

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        EdgeToEdge.enable(this);
        setContentView(R.layout.activity_device);
        ViewCompat.setOnApplyWindowInsetsListener(findViewById(R.id.activity_device), (v, insets) -> {
            Insets systemBars = insets.getInsets(WindowInsetsCompat.Type.systemBars());
            v.setPadding(systemBars.left, systemBars.top, systemBars.right, systemBars.bottom);
            return insets;
        });

        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.LOLLIPOP) {
            getWindow().setStatusBarColor(0xFF01a4ef);
        }

        WindowInsetsControllerCompat windowInsetsController =
                ViewCompat.getWindowInsetsController(getWindow().getDecorView());
        if(windowInsetsController!=null){
            windowInsetsController.setAppearanceLightStatusBars(false);
        }

        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.LOLLIPOP) {
            getWindow().setNavigationBarColor(0xFFFFFFFF);
        }
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.Q) {
            getWindow().setNavigationBarContrastEnforced(false);
        }

        scrollView = findViewById(R.id.sv_receive);
        receiveDataTextView = findViewById(R.id.tv_receive_data);
        scrollCheckBox = findViewById(R.id.cb_scroll);
        hexRevCheckBox = findViewById(R.id.cb_hex_rev);
        hexSendCheckBox = findViewById(R.id.cb_hex_send);
        sendDataEditText = findViewById(R.id.et_send);
        findViewById(R.id.iv_back).setOnClickListener((View view)-> {
            finish();
            overridePendingTransition(R.anim.finish_enter_anim, R.anim.finish_exit_anim);
        });
        findViewById(R.id.bt_send).setOnClickListener((View view)->{
            String data = sendDataEditText.getText().toString();
            if (hexSendCheckBox.isChecked()) {
                //send hex
                data = data.replace(" ","").replace("\r","").replace("\n","");
                if (data.isEmpty()) {
                    showAlert("提示", "请输入要发送的数据",()->{});
                    return;
                }
                if (data.length() % 2 != 0) {
                    showAlert("提示", "长度错误，长度只能是双数",()->{});
                    return;
                }
                if (data.length() > 488) {
                    showAlert("提示", "最多只能发送244字节",()->{});
                    return;
                }
                if (!Pattern.compile("^[0-9a-fA-F]+$").matcher(data).matches()) {
                    showAlert("提示", "格式错误，只能是0-9、a-f、A-F",()->{});
                    return;
                }
                ECBLE.writeBLECharacteristicValue(data, true);
            } else {
                //send string
                if (data.isEmpty()) {
                    showAlert("提示", "请输入要发送的数据",()->{});
                    return;
                }
                String tempSendData = data.replace("\n","\r\n");
                if (tempSendData.length() > 244) {
                    showAlert("提示", "最多只能发送244字节",()->{});
                    return;
                }
                ECBLE.writeBLECharacteristicValue(tempSendData, false);
            }
        });
        findViewById(R.id.bt_clear).setOnClickListener((View view2)-> receiveDataTextView.setText(""));
        ((RadioButton)findViewById(R.id.rb_utf8)).setOnCheckedChangeListener((compoundButton, b) -> {
            if(b){
                ECBLE.setChineseTypeUTF8();
            }else {
                ECBLE.setChineseTypeGBK();
            }
        });
        ECBLE.setChineseTypeGBK();

        ECBLE.onBLEConnectionStateChange((boolean ok, int errCode, String errMsg)-> runOnUiThread(()->{
            showToast("蓝牙断开连接");
            showAlert("提示","蓝牙断开连接",()->{});
        }));
        ECBLE.onBLECharacteristicValueChange((String str,String strHex)-> runOnUiThread(()->{
            @SuppressLint("SimpleDateFormat") String timeStr = new SimpleDateFormat("[HH:mm:ss,SSS]: ").format(new Date(System.currentTimeMillis()));
            String nowStr = receiveDataTextView.getText().toString();
            receiveDataTextView.setTextIsSelectable(false);
            if (hexRevCheckBox.isChecked()) {
                receiveDataTextView.setText(nowStr + timeStr + strHex.replaceAll("(.{2})","$1 ") + "\r\n");
            } else {
                receiveDataTextView.setText(nowStr + timeStr + str + "\r\n");
            }
            receiveDataTextView.setTextIsSelectable(true);
            if (scrollCheckBox.isChecked()) {
                scrollView.post(()-> scrollView.fullScroll(ScrollView.FOCUS_DOWN));
            }
        }));
    }

    @Override
    protected void onDestroy() {
        super.onDestroy();
        Log.e("onDestroy","onDestroy");
        ECBLE.onBLECharacteristicValueChange((str,strHex)->{});
        ECBLE.onBLEConnectionStateChange((ok,errCode,errMsg)->{});
        ECBLE.closeBLEConnection();
    }

    void showToast(String text){
        Toast.makeText(this,text,Toast.LENGTH_SHORT).show();
    }

    void showAlert(String title,String content,Runnable callback){
        new AlertDialog.Builder(this)
                .setTitle(title)
                .setMessage(content)
                .setPositiveButton("OK",  (dialogInterface , i)-> new Thread(callback).start())
                .setCancelable(false)
                .create().show();
    }

    @Override
    public void onBackPressed() {
        super.onBackPressed();
        overridePendingTransition(R.anim.finish_enter_anim, R.anim.finish_exit_anim);
    }
}
