package com.example.ble;

import android.annotation.SuppressLint;
import android.bluetooth.BluetoothAdapter;
import android.bluetooth.BluetoothDevice;
import android.bluetooth.BluetoothSocket;
import android.os.Bundle;
import android.widget.Button;
import android.widget.Toast;
import androidx.appcompat.app.AppCompatActivity;

import java.io.IOException;
import java.io.OutputStream;
import java.util.Set;
import java.util.UUID;

public class MainActivity extends AppCompatActivity {

    // SPP 标准蓝牙串口 UUID
    private static final UUID SPP_UUID = UUID.fromString("00001101-0000-1000-8000-00805F9B34FB");

    // 你的单片机蓝牙模块名称（在手机系统蓝牙配对列表中看到的名字）
    private static final String TARGET_DEVICE_NAME = "JDY-31-SPP";

    private BluetoothAdapter bluetoothAdapter;
    private BluetoothSocket socket;
    private OutputStream outputStream;

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        setContentView(R.layout.activity_main);

        bluetoothAdapter = BluetoothAdapter.getDefaultAdapter();

        Button btnConnect = findViewById(R.id.btnConnect);
        Button btnCmd1 = findViewById(R.id.btnCmd1);
        Button btnCmd2 = findViewById(R.id.btnCmd2);
        Button btnCmd3 = findViewById(R.id.btnCmd3);

        // 1. 连接按钮
        btnConnect.setOnClickListener(v -> connectToDevice());

        // 2. 按键发送不同指令
        btnCmd1.setOnClickListener(v -> sendCommand("CMD_ON\n"));
        btnCmd2.setOnClickListener(v -> sendCommand("CMD_OFF\n"));
        btnCmd3.setOnClickListener(v -> sendCommand("CMD_RESET\n"));
    }

    @SuppressLint("MissingPermission")
    private void connectToDevice() {
        if (bluetoothAdapter == null || !bluetoothAdapter.isEnabled()) {
            Toast.makeText(this, "请先开启手机蓝牙", Toast.LENGTH_SHORT).show();
            return;
        }

        // 获取手机已经配对过的设备
        Set<BluetoothDevice> pairedDevices = bluetoothAdapter.getBondedDevices();
        BluetoothDevice targetDevice = null;

        for (BluetoothDevice device : pairedDevices) {
            if (TARGET_DEVICE_NAME.equals(device.getName())) {
                targetDevice = device;
                break;
            }
        }

        if (targetDevice == null) {
            Toast.makeText(this, "未找到已配对的设备: " + TARGET_DEVICE_NAME + "，请先在手机设置中配对", Toast.LENGTH_LONG).show();
            return;
        }

        // 蓝牙连接属于耗时网络 IO，必须在子线程中执行
        final BluetoothDevice finalDevice = targetDevice;
        new Thread(() -> {
            try {
                socket = finalDevice.createRfcommSocketToServiceRecord(SPP_UUID);
                bluetoothAdapter.cancelDiscovery(); // 连接前关闭扫描，提高连接成功率
                socket.connect();
                outputStream = socket.getOutputStream();

                runOnUiThread(() -> Toast.makeText(MainActivity.this, "连接单片机成功！", Toast.LENGTH_SHORT).show());
            } catch (IOException e) {
                e.printStackTrace();
                runOnUiThread(() -> Toast.makeText(MainActivity.this, "连接失败：" + e.getMessage(), Toast.LENGTH_SHORT).show());
            }
        }).start();
    }

    // 向单片机发送字符串指令
    private void sendCommand(String command) {
        if (outputStream == null) {
            Toast.makeText(this, "蓝牙未连接，请先点击连接", Toast.LENGTH_SHORT).show();
            return;
        }

        // 异步发送数据
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
        // 退出页面时释放资源
        try {
            if (outputStream != null) outputStream.close();
            if (socket != null) socket.close();
        } catch (IOException e) {
            e.printStackTrace();
        }
    }
}