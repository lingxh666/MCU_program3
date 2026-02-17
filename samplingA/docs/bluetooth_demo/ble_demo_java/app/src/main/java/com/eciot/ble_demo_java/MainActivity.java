package com.eciot.ble_demo_java;

import android.annotation.SuppressLint;
import android.content.Context;
import android.os.Build;
import android.os.Bundle;
import android.view.View;
import android.widget.ArrayAdapter;

import androidx.activity.EdgeToEdge;
import androidx.annotation.NonNull;
import androidx.appcompat.app.AppCompatActivity;
import androidx.core.graphics.Insets;
import androidx.core.view.ViewCompat;
import androidx.core.view.WindowInsetsCompat;
import androidx.core.view.WindowInsetsControllerCompat;

import java.util.List;

import androidx.annotation.Nullable;
import androidx.appcompat.app.AlertDialog;
import androidx.swiperefreshlayout.widget.SwipeRefreshLayout;

import android.app.ProgressDialog;
import android.content.Intent;
import android.os.Handler;
import android.provider.Settings;
import android.util.Log;
import android.view.LayoutInflater;
import android.view.ViewGroup;
import android.widget.AdapterView;
import android.widget.ImageView;
import android.widget.ListView;
import android.widget.TextView;
import android.widget.Toast;

import java.util.ArrayList;

import pub.devrel.easypermissions.AppSettingsDialog;
import pub.devrel.easypermissions.EasyPermissions;

public class MainActivity extends AppCompatActivity implements EasyPermissions.PermissionCallbacks {

    static class DeviceInfo {
        String id;
        String name;
        String mac;
        int rssi;

        DeviceInfo(String id, String name, String mac, int rssi) {
            this.id = id;
            this.name = name;
            this.mac = mac;
            this.rssi = rssi;
        }
    }

    static class Adapter extends ArrayAdapter<DeviceInfo> {
        private final int myResource;

        public Adapter(@NonNull Context context, int resource, List<DeviceInfo> deviceListData) {
            super(context, resource, deviceListData);
            myResource = resource;
        }

        @SuppressLint("DefaultLocale")
        @NonNull
        @Override
        public View getView(int position, @Nullable View convertView, @NonNull ViewGroup parent) {
            DeviceInfo deviceInfo = getItem(position);
            String name = "";
            int rssi = 0;
            if (deviceInfo != null) {
                name = deviceInfo.name;
                rssi = deviceInfo.rssi;
            }
            @SuppressLint("ViewHolder") View view = LayoutInflater.from(getContext()).inflate(myResource, parent, false);
            ImageView headImg = view.findViewById(R.id.iv_type);
            if (name == null || name.isEmpty()) {
                headImg.setImageResource(R.drawable.ble);
            } else if (
                    (name.startsWith("@") && (name.length() == 11)) ||
                            (name.startsWith("BT_") && (name.length() == 15))
            ) {
                headImg.setImageResource(R.drawable.ecble);
            } else {
                headImg.setImageResource(R.drawable.ble);
            }
            ((TextView) view.findViewById(R.id.tv_name)).setText(name);
            ((TextView) view.findViewById(R.id.tv_rssi)).setText(String.format("%d", rssi));
            ImageView rssiImg = view.findViewById(R.id.iv_rssi);
            if (rssi >= -41) rssiImg.setImageResource(R.drawable.s5);
            else if (rssi >= -55) rssiImg.setImageResource(R.drawable.s4);
            else if (rssi >= -65) rssiImg.setImageResource(R.drawable.s3);
            else if (rssi >= -75) rssiImg.setImageResource(R.drawable.s2);
            else rssiImg.setImageResource(R.drawable.s1);
            return view;
        }
    }

    List<DeviceInfo> deviceListData = new ArrayList<>();
    List<DeviceInfo> deviceListDataShow = new ArrayList<>();
    Adapter listViewAdapter = null;
    ProgressDialog connectDialog = null;

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        EdgeToEdge.enable(this);
        setContentView(R.layout.activity_main);

        uiInit();

    }

    @Override
    protected void onStart() {
        super.onStart();

        deviceListData.clear();
        deviceListDataShow.clear();
        listViewAdapter.notifyDataSetChanged();

        openBluetoothAdapter();
    }

    @Override
    protected void onStop() {
        super.onStop();
//        ECBLE.stopBluetoothDevicesDiscovery(this);
    }

    void uiInit() {
        ViewCompat.setOnApplyWindowInsetsListener(findViewById(R.id.activity_main), (v, insets) -> {
            Insets systemBars = insets.getInsets(WindowInsetsCompat.Type.systemBars());
            v.setPadding(systemBars.left, systemBars.top, systemBars.right, systemBars.bottom);
            return insets;
        });

        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.LOLLIPOP) {
            getWindow().setStatusBarColor(0xFF01a4ef);
        }

        WindowInsetsControllerCompat windowInsetsController =
                ViewCompat.getWindowInsetsController(getWindow().getDecorView());
        if (windowInsetsController != null) {
            windowInsetsController.setAppearanceLightStatusBars(false);
        }

        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.LOLLIPOP) {
            getWindow().setNavigationBarColor(0xFFFFFFFF);
        }
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.Q) {
            getWindow().setNavigationBarContrastEnforced(false);
        }

        SwipeRefreshLayout swipeRefreshLayout = findViewById(R.id.swipe_layout);
        swipeRefreshLayout.setColorSchemeColors(0x01a4ef);
        swipeRefreshLayout.setOnRefreshListener(() -> {
            deviceListData.clear();
            deviceListDataShow.clear();
            listViewAdapter.notifyDataSetChanged();
            new Handler().postDelayed(() -> {
                swipeRefreshLayout.setRefreshing(false);
                //权限
                openBluetoothAdapter();
            }, 1000);
        });

        ListView listView = findViewById(R.id.list_view);
        listViewAdapter = new Adapter(this, R.layout.list_item, deviceListDataShow);
        listView.setAdapter(listViewAdapter);
        listView.setOnItemClickListener((AdapterView<?> adapterView, View view, int i, long l) -> {
            showConnectDialog();
            DeviceInfo deviceInfo = (DeviceInfo) listView.getItemAtPosition(i);
            ECBLE.onBLEConnectionStateChange((boolean ok, int errCode, String errMsg) -> runOnUiThread(() -> {
                hideConnectDialog();
                if (ok) {
//                ECBLE.stopBluetoothDevicesDiscovery(this);
                    startActivities(new Intent[]{new Intent().setClass(this, DeviceActivity.class)});
                    overridePendingTransition(R.anim.jump_enter_anim, R.anim.jump_exit_anim);
                } else {
                    showToast("蓝牙连接失败,errCode=" + errCode + ",errMsg=" + errMsg);
                    showAlert("提示", "蓝牙连接失败,errCode=" + errCode + ",errMsg=" + errMsg, () -> {
                    });
                }
            }));
            ECBLE.createBLEConnection(this, deviceInfo.id);
        });
        listRefresh();
    }

    void listRefresh() {
        new Handler().postDelayed(() -> {
            deviceListDataShow.clear();
            for (DeviceInfo tempDevice : deviceListData) {
                deviceListDataShow.add(new DeviceInfo(tempDevice.id, tempDevice.name, tempDevice.mac, tempDevice.rssi));
            }
            listViewAdapter.notifyDataSetChanged();
            listRefresh();
        }, 400);
    }

    void showAlert(String title, String content, Runnable callback) {
        new AlertDialog.Builder(this)
                .setTitle(title)
                .setMessage(content)
                .setPositiveButton("OK", (dialogInterface, i) ->
                        new Thread(callback).start()
                )
                .setCancelable(false)
                .create().show();
    }

    void showConnectDialog() {
        if (connectDialog == null) {
            connectDialog = new ProgressDialog(MainActivity.this);
            connectDialog.setMessage("连接中...");
            connectDialog.setCancelable(false);
        }
        connectDialog.show();
    }

    void hideConnectDialog() {
        if (connectDialog != null) connectDialog.dismiss();
    }

    void showToast(String text) {
        Toast.makeText(this, text, Toast.LENGTH_SHORT).show();
    }

    void openBluetoothAdapter() {
        ECBLE.onBluetoothAdapterStateChange((boolean ok, int errCode, String errMsg) -> runOnUiThread(() -> {
            if (!ok) {
                showAlert("提示", "openBluetoothAdapter error,errCode=" + errCode + ",errMsg=" + errMsg, () -> runOnUiThread(() -> {
                    if (errCode == 10001) {
                        //蓝牙开关没有打开
                        startActivity(new Intent(Settings.ACTION_BLUETOOTH_SETTINGS));
                    }
                    if (errCode == 10002) {
                        //定位开关没有打开
                        startActivity(new Intent(Settings.ACTION_LOCATION_SOURCE_SETTINGS));
                    }
                    //获取定位权限失败
                    if (errCode == 10003) {
                        new AppSettingsDialog.Builder(this)
                                .setTitle("提示")
                                .setRationale("请打开应用的定位权限")
                                .build().show();
                    }
                    //获取蓝牙连接附近设备的权限失败
                    if (errCode == 10004) {
                        new AppSettingsDialog.Builder(this)
                                .setTitle("提示")
                                .setRationale("请打开应用的蓝牙权限，允许应用使用蓝牙连接附近的设备")
                                .build().show();
                    }
                }));
            } else {
//                showToast("openBluetoothAdapter ok");
                Log.e("openBluetoothAdapter", "ok");
                startBluetoothDevicesDiscovery();
            }
        }));
        ECBLE.openBluetoothAdapter(this);
    }

    void startBluetoothDevicesDiscovery() {
        ECBLE.onBluetoothDeviceFound((String id, String name, String mac, int rssi) -> runOnUiThread(() -> {
//            Log.e("Discovery", name + "|" + mac + "|" + rssi);
            boolean isExist = false;
            for (DeviceInfo tempDevice : deviceListData) {
                if (tempDevice.id.equals(id)) {
                    tempDevice.rssi = rssi;
                    tempDevice.name = name;
                    isExist = true;
                    break;
                }
            }
            if (!isExist) {
                deviceListData.add(new DeviceInfo(id, name, mac, rssi));
            }
        }));
        ECBLE.startBluetoothDevicesDiscovery(this);
    }

    @Override
    public void onRequestPermissionsResult(int requestCode, @NonNull String[] permissions, @NonNull int[] grantResults) {
        super.onRequestPermissionsResult(requestCode, permissions, grantResults);
        EasyPermissions.onRequestPermissionsResult(requestCode, permissions, grantResults, this);
    }

    @Override
    public void onPermissionsGranted(int requestCode, @NonNull List<String> perms) {
        ECBLE.onPermissionsGranted(this, requestCode, perms);
    }

    @Override
    public void onPermissionsDenied(int requestCode, @NonNull List<String> perms) {
        //获取权限失败
//        Log.e("onPermissionsDenied","requestCode="+requestCode);
//        for (String perm : perms) {
//            Log.e("perm:",perm);
//        }
        ECBLE.onPermissionsDenied(requestCode, perms);
    }
}
