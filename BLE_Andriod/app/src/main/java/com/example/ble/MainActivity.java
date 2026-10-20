package com.example.ble;

import android.Manifest;
import android.app.AlertDialog;
import android.bluetooth.BluetoothAdapter;
import android.bluetooth.BluetoothDevice;
import android.bluetooth.BluetoothSocket;
import android.content.pm.PackageManager;
import android.content.res.ColorStateList;
import android.graphics.Color;
import android.os.Build;
import android.os.Bundle;
import android.widget.Button;
import android.widget.TextView;
import android.widget.Toast;
import androidx.appcompat.app.AppCompatActivity;
import androidx.core.app.ActivityCompat;
import androidx.core.content.ContextCompat;

import java.io.BufferedReader;
import java.io.IOException;
import java.io.InputStream;
import java.io.InputStreamReader;
import java.io.OutputStream;
import java.util.ArrayList;
import java.util.List;
import java.util.Locale;
import java.util.Set;
import java.util.UUID;

public class MainActivity extends AppCompatActivity {

    private static final UUID SPP_UUID = UUID.fromString("00001101-0000-1000-8000-00805F9B34FB");

    private BluetoothAdapter bluetoothAdapter;
    private BluetoothSocket socket;
    private OutputStream outputStream;
    private volatile boolean isConnected = false;

    // UI 控件
    private TextView tvStatus, tvVavg, tvVpp, tvVmax, tvFreq, tvYScaleLabel, tvLog;
    private Button btnConnect, btnY1, btnY2, btnY4, btnY8;

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        if (getSupportActionBar() != null) {
            getSupportActionBar().hide(); // 隐藏系统自带顶部栏，界面更美观
        }
        setContentView(R.layout.activity_main);

        bluetoothAdapter = BluetoothAdapter.getDefaultAdapter();
        initViews();
        requestBluetoothPermissions();
    }

    private void initViews() {
        tvStatus = findViewById(R.id.tvStatus);
        tvVavg = findViewById(R.id.tvVavg);
        tvVpp = findViewById(R.id.tvVpp);
        tvVmax = findViewById(R.id.tvVmax);
        tvFreq = findViewById(R.id.tvFreq);
        tvYScaleLabel = findViewById(R.id.tvYScaleLabel);
        tvLog = findViewById(R.id.tvLog);

        btnConnect = findViewById(R.id.btnConnect);
        btnY1 = findViewById(R.id.btnY1);
        btnY2 = findViewById(R.id.btnY2);
        btnY4 = findViewById(R.id.btnY4);
        btnY8 = findViewById(R.id.btnY8);

        // 蓝牙连接按钮（点击弹出已配对设备列表，支持直接选 JDY-31）
        btnConnect.setOnClickListener(v -> showDeviceSelectDialog());

        // 1. 本地显示页面切换指令
        findViewById(R.id.btnPageWave).setOnClickListener(v -> sendCommand("W", "切换至 [波形显示页]"));
        findViewById(R.id.btnPageParam).setOnClickListener(v -> sendCommand("M", "切换至 [参数测量页]"));
        findViewById(R.id.btnPageFft).setOnClickListener(v -> sendCommand("F", "切换至 [FFT 频谱图像页]"));
        findViewById(R.id.btnPagePrev).setOnClickListener(v -> sendCommand("P", "本地显示 [上一页面]"));
        findViewById(R.id.btnPageNext).setOnClickListener(v -> sendCommand("N", "本地显示 [下一页面]"));

        // 2. Y轴 1、2、4、8倍缩放控制
        btnY1.setOnClickListener(v -> {
            updateYScaleUI(1);
            sendCommand("1", "Y轴缩放设为 ×1 倍");
        });
        btnY2.setOnClickListener(v -> {
            updateYScaleUI(2);
            sendCommand("2", "Y轴缩放设为 ×2 倍");
        });
        btnY4.setOnClickListener(v -> {
            updateYScaleUI(4);
            sendCommand("4", "Y轴缩放设为 ×4 倍");
        });
        btnY8.setOnClickListener(v -> {
            updateYScaleUI(8);
            sendCommand("8", "Y轴缩放设为 ×8 倍");
        });

        // 3. X轴缩放与左右平移控制
        findViewById(R.id.btnXZoomIn).setOnClickListener(v -> sendCommand("+", "X轴时基放大 (+)"));
        findViewById(R.id.btnXZoomOut).setOnClickListener(v -> sendCommand("-", "X轴时基缩小 (-)"));
        findViewById(R.id.btnXLeft).setOnClickListener(v -> sendCommand("L", "X轴波形左移"));
        findViewById(R.id.btnXRight).setOnClickListener(v -> sendCommand("R", "X轴波形右移"));
        findViewById(R.id.btnXReset).setOnClickListener(v -> sendCommand("0", "X/Y轴缩放平移复位"));
        findViewById(R.id.btnRunStop).setOnClickListener(v -> sendCommand("S", "切换 [运行/暂停] 状态"));
    }

    // 更新 Y 轴按键的高亮与普通浅蓝状态
    private void updateYScaleUI(int scale) {
        tvYScaleLabel.setText("Y轴垂直缩放 / 分辨率控制 (当前: ×" + scale + ")");
        int activeColor = Color.parseColor("#2563EB"); // 选中时：深蓝
        int normalColor = Color.parseColor("#BAE6FD"); // 未选中时：浅蓝
        int textColorActive = Color.parseColor("#FFFFFF");
        int textColorNormal = Color.parseColor("#0F172A");

        Button[] buttons = {btnY1, btnY2, btnY4, btnY8};
        int[] scales = {1, 2, 4, 8};

        for (int i = 0; i < buttons.length; i++) {
            boolean isSelected = (scales[i] == scale);
            buttons[i].setBackgroundTintList(ColorStateList.valueOf(isSelected ? activeColor : normalColor));
            buttons[i].setTextColor(isSelected ? textColorActive : textColorNormal);
        }
    }
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

    // 弹出已配对设备选择框（不用死记蓝牙名字，直接点选 JDY-31）
    private void showDeviceSelectDialog() {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) {
            if (ActivityCompat.checkSelfPermission(this, Manifest.permission.BLUETOOTH_CONNECT)
                    != PackageManager.PERMISSION_GRANTED) {
                Toast.makeText(this, "请先授予蓝牙连接权限！", Toast.LENGTH_SHORT).show();
                requestBluetoothPermissions();
                return;
            }
        }

        if (bluetoothAdapter == null) {
            Toast.makeText(this, "当前设备不支持蓝牙", Toast.LENGTH_SHORT).show();
            return;
        }
        if (!bluetoothAdapter.isEnabled()) {
            Toast.makeText(this, "请先在手机下拉栏开启蓝牙", Toast.LENGTH_SHORT).show();
            return;
        }

        try {
            Set<BluetoothDevice> pairedDevices = bluetoothAdapter.getBondedDevices();
            if (pairedDevices == null || pairedDevices.isEmpty()) {
                Toast.makeText(this, "未找到已配对设备，请先在手机设置中配对 JDY-31", Toast.LENGTH_LONG).show();
                return;
            }

            List<BluetoothDevice> deviceList = new ArrayList<>(pairedDevices);
            String[] deviceNames = new String[deviceList.size()];
            for (int i = 0; i < deviceList.size(); i++) {
                BluetoothDevice d = deviceList.get(i);
                deviceNames[i] = d.getName() + "\n(" + d.getAddress() + ")";
            }

            new AlertDialog.Builder(this)
                    .setTitle("选择要连接的蓝牙模块 (JDY-31)")
                    .setItems(deviceNames, (dialog, which) -> connectToSelectedDevice(deviceList.get(which)))
                    .setNegativeButton("取消", null)
                    .show();

        } catch (SecurityException e) {
            Toast.makeText(this, "权限不足，无法读取蓝牙列表", Toast.LENGTH_SHORT).show();
        }
    }

    private void connectToSelectedDevice(BluetoothDevice device) {
        tvStatus.setText("状态：正在连接...");
        tvStatus.setTextColor(Color.parseColor("#F59E0B"));

        new Thread(() -> {
            try {
                closeConnection();
                socket = device.createRfcommSocketToServiceRecord(SPP_UUID);
                bluetoothAdapter.cancelDiscovery();
                socket.connect();
                outputStream = socket.getOutputStream();
                isConnected = true;

                String devName = device.getName();
                runOnUiThread(() -> {
                    tvStatus.setText("状态：已连接 (" + devName + ")");
                    tvStatus.setTextColor(Color.parseColor("#10B981"));
                    btnConnect.setText("重新连接");
                    Toast.makeText(MainActivity.this, "连接成功！", Toast.LENGTH_SHORT).show();
                });

                // 启动后台接收线程，实时读取单片机发来的电压、频率数据
                startReceiveThread(socket.getInputStream());

            } catch (IOException | SecurityException e) {
                isConnected = false;
                runOnUiThread(() -> {
                    tvStatus.setText("状态：连接失败，请重试");
                    tvStatus.setTextColor(Color.parseColor("#FF6B6B"));
                    Toast.makeText(MainActivity.this, "连接失败: " + e.getMessage(), Toast.LENGTH_SHORT).show();
                });
            }
        }).start();
    }

    // 实时接收单片机串口发来的测量数据 (格式: Vavg,Vpp,Vmax,Freq\n)
    private void startReceiveThread(InputStream inputStream) {
        new Thread(() -> {
            BufferedReader reader = new BufferedReader(new InputStreamReader(inputStream));
            while (isConnected) {
                try {
                    String line = reader.readLine();
                    if (line != null && !line.trim().isEmpty()) {
                        parseAndDisplayData(line.trim());
                    }
                } catch (IOException e) {
                    isConnected = false;
                    runOnUiThread(() -> {
                        tvStatus.setText("状态：蓝牙连接已断开");
                        tvStatus.setTextColor(Color.parseColor("#FF6B6B"));
                    });
                    break;
                }
            }
        }).start();
    }

    // 解析单片机上报的数据并自动换算频率单位 (0Hz - 5MHz)
    private void parseAndDisplayData(String rawLine) {
        runOnUiThread(() -> {
            tvLog.setText("收到数据: " + rawLine);
            try {
                String clean = rawLine.replaceAll("[a-zA-Z:=\\s]", "");
                String[] parts = clean.split(",");

                if (parts.length >= 4) {
                    double vavg = Double.parseDouble(parts[0]);
                    double vpp = Double.parseDouble(parts[1]);
                    double vmax = Double.parseDouble(parts[2]);
                    double freq = Double.parseDouble(parts[3]);

                    tvVavg.setText(String.format(Locale.US, "%.3f V", vavg));
                    tvVpp.setText(String.format(Locale.US, "%.3f V", vpp));
                    tvVmax.setText(String.format(Locale.US, "%.3f V", vmax));
                    tvFreq.setText(formatFrequency(freq));
                } else if (parts.length == 3) {
                    // 兼容只发 3 个参数: Vavg,Vpp,Freq
                    double vavg = Double.parseDouble(parts[0]);
                    double vpp = Double.parseDouble(parts[1]);
                    double freq = Double.parseDouble(parts[2]);

                    tvVavg.setText(String.format(Locale.US, "%.3f V", vavg));
                    tvVpp.setText(String.format(Locale.US, "%.3f V", vpp));
                    tvFreq.setText(formatFrequency(freq));
                }
            } catch (Exception ignored) {
                // 非标准数值帧仅在底部监控栏显示，不引起崩溃
            }
        });
    }

    // 自动格式化 0Hz - 5MHz 频率显示
    private String formatFrequency(double hz) {
        if (hz >= 1_000_000.0) {
            return String.format(Locale.US, "%.4f MHz", hz / 1_000_000.0);
        } else if (hz >= 1_000.0) {
            return String.format(Locale.US, "%.3f kHz", hz / 1_000.0);
        } else {
            return String.format(Locale.US, "%.1f Hz", hz);
        }
    }

    // 发送控制指令到单片机
    private void sendCommand(String cmd, String desc) {
        if (!isConnected || outputStream == null) {
            Toast.makeText(this, "请先点击右上角连接蓝牙！", Toast.LENGTH_SHORT).show();
            return;
        }

        new Thread(() -> {
            try {
                outputStream.write(cmd.getBytes());
                outputStream.flush();
                runOnUiThread(() -> tvLog.setText("已发送指令 ['" + cmd + "'] -> " + desc));
            } catch (IOException e) {
                runOnUiThread(() -> Toast.makeText(MainActivity.this, "发送失败，连接可能已断开", Toast.LENGTH_SHORT).show());
            }
        }).start();
    }

    private void closeConnection() {
        isConnected = false;
        try {
            if (outputStream != null) outputStream.close();
            if (socket != null) socket.close();
        } catch (IOException ignored) {
        }
    }

    @Override
    protected void onDestroy() {
        super.onDestroy();
        closeConnection();
    }
}