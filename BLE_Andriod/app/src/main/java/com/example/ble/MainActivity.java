package com.example.ble;

import android.Manifest;
import android.bluetooth.BluetoothAdapter;
import android.bluetooth.BluetoothDevice;
import android.bluetooth.BluetoothSocket;
import android.content.pm.PackageManager;
import android.os.Build;
import android.os.Bundle;
import android.widget.Button;
import android.widget.Toast;
import androidx.appcompat.app.AppCompatActivity;
import androidx.core.app.ActivityCompat;
import androidx.core.content.ContextCompat;

import java.io.IOException;
import java.io.OutputStream;
import java.util.Set;
import java.util.UUID;

public class MainActivity extends AppCompatActivity {

    private static final UUID SPP_UUID = UUID.fromString("00001101-0000-1000-8000-00805F9B34FB");
    // 改为你蓝牙模块的实际广播名称（如 JDY-31-SPP）
    private static final String TARGET_DEVICE_NAME = "JDY-31-SPP";

    private BluetoothAdapter bluetoothAdapter;
    private BluetoothSocket socket;
    private OutputStream outputStream;

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        setContentView(R.layout.activity_main);

        bluetoothAdapter = BluetoothAdapter.getDefaultAdapter();

        // 启动时主动申请 Android 12+ 蓝牙权限，防止闪退
        requestBluetoothPermissions();

        Button btnConnect = findViewById(R.id.btnConnect);
        Button btnCmd1 = findViewById(R.id.btnCmd1);
        Button btnCmd2 = findViewById(R.id.btnCmd2);
        Button btnCmd3 = findViewById(R.id.btnCmd3);

        btnConnect.setOnClickListener(v -> connectToDevice());
        btnCmd1.setOnClickListener(v -> sendCommand("CMD_ON\n"));
        btnCmd2.setOnClickListener(v -> sendCommand("CMD_OFF\n"));
        btnCmd3.setOnClickListener(v -> sendCommand("CMD_RESET\n"));
    }

    // 动态权限申请逻辑
    private void requestBluetoothPermissions() {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) {
            if (ContextCompat.checkSelfPermission(this, Manifest.permission.BLUETOOTH_CONNECT)
                    != PackageManager.PERMISSION_GRANTED) {
                ActivityCompat.requestPermissions(this, new String[]{
                        Manifest.permission.BLUETOOTH_CONNECT,
                        Manifest.permission.BLUETOOTH_SCAN
                }, 100);
            }
        }
    }

    private void connectToDevice() {
        // 1. 最先检查权限，无权限立即拦截并请求，绝不执行任何蓝牙 API
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) {
            if (ActivityCompat.checkSelfPermission(this, Manifest.permission.BLUETOOTH_CONNECT)
                    != PackageManager.PERMISSION_GRANTED) {
                Toast.makeText(this, "请先授予蓝牙连接权限！", Toast.LENGTH_SHORT).show();
                requestBluetoothPermissions();
                return;
            }
        }

        // 2. 确认拥有权限后，才能安全调用 isEnabled()
        if (bluetoothAdapter == null) {
            Toast.makeText(this, "该设备不支持蓝牙", Toast.LENGTH_SHORT).show();
            return;
        }
        if (!bluetoothAdapter.isEnabled()) {
            Toast.makeText(this, "请先在手机下拉菜单开启蓝牙开关", Toast.LENGTH_SHORT).show();
            return;
        }

        try {
            Set<BluetoothDevice> pairedDevices = bluetoothAdapter.getBondedDevices();
            BluetoothDevice targetDevice = null;

            if (pairedDevices != null) {
                for (BluetoothDevice device : pairedDevices) {
                    if (TARGET_DEVICE_NAME.equals(device.getName())) {
                        targetDevice = device;
                        break;
                    }
                }
            }

            if (targetDevice == null) {
                Toast.makeText(this, "未找到已配对的设备: " + TARGET_DEVICE_NAME + "，请先在系统设置配对", Toast.LENGTH_LONG).show();
                return;
            }

            final BluetoothDevice finalDevice = targetDevice;
            new Thread(() -> {
                try {
                    socket = finalDevice.createRfcommSocketToServiceRecord(SPP_UUID);
                    bluetoothAdapter.cancelDiscovery();
                    socket.connect();
                    outputStream = socket.getOutputStream();

                    runOnUiThread(() -> Toast.makeText(MainActivity.this, "连接单片机成功！", Toast.LENGTH_SHORT).show());
                } catch (IOException e) {
                    e.printStackTrace();
                    runOnUiThread(() -> Toast.makeText(MainActivity.this, "连接失败：" + e.getMessage(), Toast.LENGTH_SHORT).show());
                } catch (SecurityException se) {
                    // 兜底捕获权限异常，避免闪退
                    runOnUiThread(() -> Toast.makeText(MainActivity.this, "缺少权限，无法建立连接", Toast.LENGTH_SHORT).show());
                }
            }).start();

        } catch (SecurityException e) {
            // 捕获 getBondedDevices() 可能抛出的异常
            Toast.makeText(this, "权限不足，无法读取已配对设备", Toast.LENGTH_SHORT).show();
        }
    }
    private void sendCommand(String command) {
        if (outputStream == null) {
            Toast.makeText(this, "蓝牙未连接，请先点击连接", Toast.LENGTH_SHORT).show();
            return;
        }

        new Thread(() -> {
            try {
                outputStream.write(command.getBytes());
                outputStream.flush();
                runOnUiThread(() -> Toast.makeText(MainActivity.this, "已发送: " + command.trim(), Toast.LENGTH_SHORT).show());
            } catch (IOException e) {
                e.printStackTrace();
                runOnUiThread(() -> Toast.makeText(MainActivity.this, "发送失败", Toast.LENGTH_SHORT).show());
            }
        }).start();
    }

    @Override
    protected void onDestroy() {
        super.onDestroy();
        try {
            if (outputStream != null) outputStream.close();
            if (socket != null) socket.close();
        } catch (IOException e) {
            e.printStackTrace();
        }
    }
}