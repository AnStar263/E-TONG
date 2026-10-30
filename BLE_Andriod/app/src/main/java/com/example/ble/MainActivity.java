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
import android.view.View;
import android.view.inputmethod.EditorInfo;
import android.widget.Button;
import android.widget.EditText;
import android.widget.ImageView;
import android.widget.LinearLayout;
import android.widget.ScrollView;
import android.widget.TextView;
import android.widget.Toast;

import androidx.appcompat.app.AppCompatActivity;
import androidx.core.app.ActivityCompat;
import androidx.core.content.ContextCompat;
import androidx.core.view.GravityCompat;
import androidx.drawerlayout.widget.DrawerLayout;

import java.io.BufferedReader;
import java.io.IOException;
import java.io.InputStream;
import java.io.InputStreamReader;
import java.io.OutputStream;
import java.text.SimpleDateFormat;
import java.util.ArrayList;
import java.util.Date;
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
    private final SimpleDateFormat timeFormat = new SimpleDateFormat("HH:mm:ss", Locale.getDefault());

    // 导航与页面布局
    private DrawerLayout drawerLayout;
    private TextView tvTopTitle;
    private ScrollView pageParameters;
    private LinearLayout pageConsole;
    private TextView navItemParam, navItemConsole;

    // 参数页面 UI 控件
    private TextView tvStatus, tvVavg, tvVpp, tvVmax, tvFreq, tvYScaleLabel, tvXScaleLabel, tvLog;
    private Button btnConnect, btnY1, btnY2, btnY4, btnY8;

    // X轴时基分辨率档位管理 (发挥部分 c 项要求)
    private final String[] xTimebaseScales = {
            "1 μs/div", "2 μs/div", "5 μs/div", "10 μs/div", "20 μs/div", "50 μs/div", "100 μs/div", "200 μs/div", "500 μs/div"
    };
    private int currentXIndex = 3; // 默认 10 μs/div

    // 控制台页面 UI 控件
    private ScrollView svConsoleLog;
    private TextView tvConsoleLog;
    private EditText etConsoleInput;

    // 本地显示页面按键
    private Button btnPageWave, btnPageParam, btnPageConsole;
    private int currentLocalPage = 0; // 0: 波形页, 1: 参数页, 2: 控制台页

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        if (getSupportActionBar() != null) {
            getSupportActionBar().hide();
        }
        setContentView(R.layout.activity_main);

        bluetoothAdapter = BluetoothAdapter.getDefaultAdapter();
        initViews();
        requestBluetoothPermissions();
    }

    private void initViews() {
        // 1. 顶部栏与侧边栏
        drawerLayout = findViewById(R.id.drawerLayout);
        ImageView btnMenu = findViewById(R.id.btnMenu);
        tvTopTitle = findViewById(R.id.tvTopTitle);
        btnConnect = findViewById(R.id.btnConnect);

        pageParameters = findViewById(R.id.pageParameters);
        pageConsole = findViewById(R.id.pageConsole);
        navItemParam = findViewById(R.id.navItemParam);
        navItemConsole = findViewById(R.id.navItemConsole);

        // 2. 参数页控件
        tvStatus = findViewById(R.id.tvStatus);
        tvVavg = findViewById(R.id.tvVavg);
        tvVpp = findViewById(R.id.tvVpp);
        tvVmax = findViewById(R.id.tvVmax);
        tvFreq = findViewById(R.id.tvFreq);
        tvYScaleLabel = findViewById(R.id.tvYScaleLabel);
        tvXScaleLabel = findViewById(R.id.tvXScaleLabel);
        tvLog = findViewById(R.id.tvLog);

        btnY1 = findViewById(R.id.btnY1);
        btnY2 = findViewById(R.id.btnY2);
        btnY4 = findViewById(R.id.btnY4);
        btnY8 = findViewById(R.id.btnY8);

        // 3. 控制台页控件
        svConsoleLog = findViewById(R.id.svConsoleLog);
        tvConsoleLog = findViewById(R.id.tvConsoleLog);
        etConsoleInput = findViewById(R.id.etConsoleInput);
        Button btnConsoleClear = findViewById(R.id.btnConsoleClear);
        Button btnConsoleSend = findViewById(R.id.btnConsoleSend);
        Button btnQuickWave = findViewById(R.id.btnQuickWave);
        Button btnQuickReset = findViewById(R.id.btnQuickReset);
        Button btnQuickStop = findViewById(R.id.btnQuickStop);

        // --- 抽屉侧边栏打开与页面选择 ---
        btnMenu.setOnClickListener(v -> drawerLayout.openDrawer(GravityCompat.START));
        navItemParam.setOnClickListener(v -> switchPage(true));
        navItemConsole.setOnClickListener(v -> switchPage(false));

        // --- 蓝牙连接按钮 ---
        btnConnect.setOnClickListener(v -> showDeviceSelectDialog());

        // --- 参数页：本地显示页面切换指令 (带UI高亮联动) ---
        btnPageWave = findViewById(R.id.btnPageWave);
        btnPageParam = findViewById(R.id.btnPageParam);
        btnPageConsole = findViewById(R.id.btnPageConsole);

        btnPageWave.setOnClickListener(v -> {
            updateLocalPageUI(0);
            sendCommand("W", "切换至 [波形页]");
        });

        btnPageParam.setOnClickListener(v -> {
            updateLocalPageUI(1);
            sendCommand("M", "切换至 [参数页]");
        });

        btnPageConsole.setOnClickListener(v -> {
            updateLocalPageUI(2);
            sendCommand("C", "切换至 [控制台页]");
        });

        findViewById(R.id.btnPagePrev).setOnClickListener(v -> {
            currentLocalPage = (currentLocalPage - 1 + 3) % 3;
            updateLocalPageUI(currentLocalPage);
            sendCommand("P", "本地显示 [上一页面]");
        });

        findViewById(R.id.btnPageNext).setOnClickListener(v -> {
            currentLocalPage = (currentLocalPage + 1) % 3;
            updateLocalPageUI(currentLocalPage);
            sendCommand("N", "本地显示 [下一页面]");
        });

        // --- 参数页：Y轴 1、2、4、8倍分辨率/垂直缩放 ---
        btnY1.setOnClickListener(v -> {
            updateYScaleUI(1);
            sendCommand("1", "Y轴分辨率设为 ×1");
        });
        btnY2.setOnClickListener(v -> {
            updateYScaleUI(2);
            sendCommand("2", "Y轴分辨率设为 ×2");
        });
        btnY4.setOnClickListener(v -> {
            updateYScaleUI(4);
            sendCommand("4", "Y轴分辨率设为 ×4");
        });
        btnY8.setOnClickListener(v -> {
            updateYScaleUI(8);
            sendCommand("8", "Y轴分辨率设为 ×8");
        });

        // --- 参数页：X轴时基分辨率与水平控制 ---
        findViewById(R.id.btnXZoomIn).setOnClickListener(v -> {
            if (currentXIndex > 0) {
                currentXIndex--;
            }
            updateXScaleUI();
            sendCommand("+", "X轴时基放大(+) -> " + xTimebaseScales[currentXIndex]);
        });
        findViewById(R.id.btnXZoomOut).setOnClickListener(v -> {
            if (currentXIndex < xTimebaseScales.length - 1) {
                currentXIndex++;
            }
            updateXScaleUI();
            sendCommand("-", "X轴时基缩小(-) -> " + xTimebaseScales[currentXIndex]);
        });
        findViewById(R.id.btnXLeft).setOnClickListener(v -> sendCommand("L", "X轴波形左移"));
        findViewById(R.id.btnXRight).setOnClickListener(v -> sendCommand("R", "X轴波形右移"));
        findViewById(R.id.btnXReset).setOnClickListener(v -> {
            currentXIndex = 3;
            updateXScaleUI();
            updateYScaleUI(1);
            sendCommand("0", "X/Y轴缩放平移复位");
        });
        findViewById(R.id.btnRunStop).setOnClickListener(v -> sendCommand("S", "切换 [运行/暂停] 状态"));

        // --- 控制台页：终端清空与自定义指令发送 ---
        btnConsoleClear.setOnClickListener(v -> tvConsoleLog.setText(""));
        btnConsoleSend.setOnClickListener(v -> sendConsoleInput());
        etConsoleInput.setOnEditorActionListener((v, actionId, event) -> {
            if (actionId == EditorInfo.IME_ACTION_SEND) {
                sendConsoleInput();
                return true;
            }
            return false;
        });

        // --- 控制台页：快捷按键 ---
        btnQuickWave.setOnClickListener(v -> sendCommand("W", "快捷指令 W"));
        btnQuickReset.setOnClickListener(v -> sendCommand("0", "快捷指令 0"));
        btnQuickStop.setOnClickListener(v -> sendCommand("S", "快捷指令 S"));

        // 初始化默认高亮第 0 页（波形页）
        updateLocalPageUI(0);
    }
    private void switchPage(boolean isParamPage) {
        if (isParamPage) {
            pageParameters.setVisibility(View.VISIBLE);
            pageConsole.setVisibility(View.GONE);
            tvTopTitle.setText("示波器 - 参数");
            navItemParam.setBackgroundColor(Color.parseColor("#E2E8F0"));
            navItemParam.setTextColor(Color.parseColor("#2563EB"));
            navItemConsole.setBackgroundColor(Color.TRANSPARENT);
            navItemConsole.setTextColor(Color.parseColor("#334155"));
        } else {
            pageParameters.setVisibility(View.GONE);
            pageConsole.setVisibility(View.VISIBLE);
            tvTopTitle.setText("示波器 - 控制台");
            navItemConsole.setBackgroundColor(Color.parseColor("#E2E8F0"));
            navItemConsole.setTextColor(Color.parseColor("#2563EB"));
            navItemParam.setBackgroundColor(Color.TRANSPARENT);
            navItemParam.setTextColor(Color.parseColor("#334155"));
        }
        drawerLayout.closeDrawer(GravityCompat.START);
    }

    private void sendConsoleInput() {
        String text = etConsoleInput.getText().toString().trim();
        if (!text.isEmpty()) {
            sendCommand(text, "终端输入");
            etConsoleInput.setText("");
        } else {
            Toast.makeText(this, "请输入有效指令", Toast.LENGTH_SHORT).show();
        }
    }

    private void appendConsoleLog(String tag, String message) {
        runOnUiThread(() -> {
            String time = timeFormat.format(new Date());
            tvConsoleLog.append("[" + time + "] [" + tag + "] " + message + "\n");
            svConsoleLog.post(() -> svConsoleLog.fullScroll(View.FOCUS_DOWN));
        });
    }

    // 更新 Y 轴高亮与标签
    private void updateYScaleUI(int scale) {
        tvYScaleLabel.setText("Y轴垂直缩放 / 分辨率控制 (当前: ×" + scale + ")");
        int activeColor = Color.parseColor("#2563EB");
        int normalColor = Color.parseColor("#BAE6FD");
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

    // 更新 X 轴时基分辨率标签
    private void updateXScaleUI() {
        tvXScaleLabel.setText("X轴时基分辨率控制 (当前: " + xTimebaseScales[currentXIndex] + ")");
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
                Toast.makeText(this, "未找到已配对设备，请先在手机设置中配对蓝牙模块", Toast.LENGTH_LONG).show();
                return;
            }

            List<BluetoothDevice> deviceList = new ArrayList<>(pairedDevices);
            String[] deviceNames = new String[deviceList.size()];
            for (int i = 0; i < deviceList.size(); i++) {
                BluetoothDevice d = deviceList.get(i);
                deviceNames[i] = d.getName() + "\n(" + d.getAddress() + ")";
            }

            new AlertDialog.Builder(this)
                    .setTitle("选择要连接的蓝牙模块")
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
        appendConsoleLog("SYS", "正在连接到设备: " + device.getName());

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
                    btnConnect.setText("已连接");
                    Toast.makeText(MainActivity.this, "连接成功！", Toast.LENGTH_SHORT).show();
                });
                appendConsoleLog("SYS", "蓝牙连接成功: " + devName);

                startReceiveThread(socket.getInputStream());

            } catch (IOException | SecurityException e) {
                isConnected = false;
                runOnUiThread(() -> {
                    tvStatus.setText("状态：连接失败，请重试");
                    tvStatus.setTextColor(Color.parseColor("#FF6B6B"));
                    btnConnect.setText("连接蓝牙");
                    Toast.makeText(MainActivity.this, "连接失败: " + e.getMessage(), Toast.LENGTH_SHORT).show();
                });
                appendConsoleLog("ERR", "连接失败: " + e.getMessage());
            }
        }).start();
    }

    private void startReceiveThread(InputStream inputStream) {
        new Thread(() -> {
            BufferedReader reader = new BufferedReader(new InputStreamReader(inputStream));
            while (isConnected) {
                try {
                    String line = reader.readLine();
                    if (line != null && !line.trim().isEmpty()) {
                        String rawLine = line.trim();
                        appendConsoleLog("RX", rawLine);
                        parseAndDisplayData(rawLine);
                    }
                } catch (IOException e) {
                    isConnected = false;
                    runOnUiThread(() -> {
                        tvStatus.setText("状态：蓝牙连接已断开");
                        tvStatus.setTextColor(Color.parseColor("#FF6B6B"));
                        btnConnect.setText("重新连接");
                    });
                    appendConsoleLog("SYS", "蓝牙连接已断开");
                    break;
                }
            }
        }).start();
    }

    // 解析单片机回传数据: Vavg,Vpp,Vmax,Freq
    private void parseAndDisplayData(String rawLine) {
        runOnUiThread(() -> {
            tvLog.setText("通信监控：收到 " + rawLine);
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
                    double vavg = Double.parseDouble(parts[0]);
                    double vpp = Double.parseDouble(parts[1]);
                    double freq = Double.parseDouble(parts[2]);

                    tvVavg.setText(String.format(Locale.US, "%.3f V", vavg));
                    tvVpp.setText(String.format(Locale.US, "%.3f V", vpp));
                    tvFreq.setText(formatFrequency(freq));
                }
            } catch (Exception ignored) {
            }
        });
    }

    // 格式化 0Hz - 800kHz 频率，确保精度直观展示
    private String formatFrequency(double hz) {
        if (hz >= 1_000.0) {
            return String.format(Locale.US, "%.3f kHz", hz / 1_000.0);
        } else {
            return String.format(Locale.US, "%.1f Hz", hz);
        }
    }

    private void sendCommand(String cmd, String desc) {
        if (!isConnected || outputStream == null) {
            Toast.makeText(this, "请先点击右上角连接蓝牙！", Toast.LENGTH_SHORT).show();
            appendConsoleLog("WARN", "发送失败：蓝牙未连接");
            return;
        }

        new Thread(() -> {
            try {
                outputStream.write(cmd.getBytes());
                outputStream.flush();
                runOnUiThread(() -> tvLog.setText("已发送指令 ['" + cmd + "'] -> " + desc));
                appendConsoleLog("TX", cmd + " (" + desc + ")");
            } catch (IOException e) {
                runOnUiThread(() -> Toast.makeText(MainActivity.this, "发送失败，连接可能已断开", Toast.LENGTH_SHORT).show());
                appendConsoleLog("ERR", "发送指令异常: " + e.getMessage());
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
    public void onBackPressed() {
        if (drawerLayout != null && drawerLayout.isDrawerOpen(GravityCompat.START)) {
            drawerLayout.closeDrawer(GravityCompat.START);
        } else {
            super.onBackPressed();
        }
    }

    @Override
    protected void onDestroy() {
        super.onDestroy();
        closeConnection();
    }

    // 更新本地页面按钮的高亮状态 (选中的变深色 #2563EB，未选中的为浅色 #BAE6FD)
    private void updateLocalPageUI(int pageIndex) {
        currentLocalPage = pageIndex;
        int activeColor = Color.parseColor("#2563EB");     // 深蓝色高亮
        int normalColor = Color.parseColor("#BAE6FD");     // 浅蓝色默认
        int textColorActive = Color.parseColor("#FFFFFF"); // 白色文字
        int textColorNormal = Color.parseColor("#0F172A"); // 深色文字

        Button[] buttons = {btnPageWave, btnPageParam, btnPageConsole};

        for (int i = 0; i < buttons.length; i++) {
            boolean isSelected = (i == pageIndex);
            buttons[i].setBackgroundTintList(ColorStateList.valueOf(isSelected ? activeColor : normalColor));
            buttons[i].setTextColor(isSelected ? textColorActive : textColorNormal);
            buttons[i].setTypeface(null, isSelected ? android.graphics.Typeface.BOLD : android.graphics.Typeface.NORMAL);
        }
    }
}