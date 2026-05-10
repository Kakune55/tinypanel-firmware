# ESP32-S3-RLCD-4.2 桌面信息站项目手册

> 适用对象：Waveshare / 微雪 ESP32-S3-RLCD-4.2 开发板  
> 项目目标：基于 18650 电池供电的桌面信息站，显示时间、天气、温湿度、电池状态、提醒消息，并尽可能兼顾低功耗。  
> 建议用途：放在 PlatformIO 项目根目录，作为 Codex / AI 编程助手的长期参考上下文。

---

## 0. 重要结论摘要

### 0.1 板子定位

这块板可以理解为：

```text
ESP32-S3 + 4.2 寸全反射 RLCD + 18650 电池座 + RTC + 温湿度 + 音频 + SD 卡
```

它适合做：

- 桌面信息站
- 电子日历
- 低功耗环境监测面板
- 消息提醒屏
- Home Assistant / IoT 面板
- 轻量级 AIoT 原型

不适合直接当成：

- 高帧率 GUI 设备
- 彩色屏设备
- 完全断电仍保持显示的电子墨水设备
- 手机级实时推送终端

### 0.2 最推荐的软件路线

第一版建议：

```text
PlatformIO + Arduino framework
+ Waveshare ST7305 初始化序列
+ 自己封装 RlcdDisplay
+ 自己写轻量 UI 层
+ HTTP 轮询消息 / 天气
+ 电池供电时 deep sleep
+ USB 供电时可切 MQTT/WebSocket 在线模式
```

第一版暂时不建议：

```text
一开始就上 LVGL
一开始做复杂设置页
一开始做秒级推送
一开始追求极限低功耗
一开始完全重写 ST7305 底层驱动
```

### 0.3 对 Waveshare 示例代码的态度

Waveshare 官方仓库资料足够完成硬件点亮和外设验证，但它更像 **BSP / Demo 包**，不是适合长期维护的产品级 SDK。

正确使用方式：

```text
用它的引脚定义
用它的 ST7305 初始化序列
用它的 framebuffer 像素映射逻辑
用它验证 ADC / RTC / SHTC3 / SD / Audio

不要照搬它的工程结构
不要让业务逻辑直接调用 RLCD_* 原始函数
不要把它的 LVGL 示例当低功耗 UI 最优实现
```

---

## 1. 板卡定义

### 1.1 基本型号

- 产品名称：ESP32-S3-RLCD-4.2
- 厂商：Waveshare / 微雪
- 主控：ESP32-S3-WROOM-1-N16R8
- CPU：Xtensa LX7 双核，最高 240MHz
- Flash：16MB
- PSRAM：8MB
- 无线：2.4GHz Wi-Fi + Bluetooth 5 LE
- 显示屏：4.2 英寸全反射 RLCD
- 分辨率：官方标称 300 × 400
- 驱动 IC：ST7305
- 显示颜色：黑 / 白，2 灰阶级别
- 通信接口：SPI
- 电池：板载 18650 电池座，带充放电管理
- 传感器：SHTC3 温湿度传感器
- RTC：PCF85063
- 音频：ES7210 ADC + ES8311 codec，双麦克风阵列，外接扬声器接口
- 存储：Micro SD / TF 卡槽

### 1.2 项目中对这块板的抽象定义

建议在代码中把它定义为：

```cpp
// Board identity
#define WAVESHARE_ESP32_S3_RLCD_42
```

从项目架构上，它不是普通 OLED 小屏，而是一个完整的低功耗信息终端：

```text
Board
├── MCU: ESP32-S3
├── Display: ST7305 RLCD, 300x400 physical, usually used as 400x300 landscape
├── Power: USB-C / 18650 battery / RTC backup battery
├── Sensors: SHTC3 temperature & humidity
├── RTC: PCF85063
├── Storage: SD card
├── Audio: mic array + codec
└── Network: Wi-Fi / BLE
```

---

## 2. 引脚与板级资源

> 注意：以下引脚主要来自 Waveshare 官方示例代码和社区配置。Codex 实现时应集中放入 `BoardConfig.h`，不要在业务代码里散落魔法数字。

### 2.1 显示屏 ST7305 / SPI

Waveshare LVGL v9 示例中这样创建显示端口：

```cpp
DisplayPort RlcdPort(12, 11, 5, 40, 41, 400, 300);
```

该构造函数参数含义为：

```cpp
DisplayPort(int mosi, int scl, int dc, int cs, int rst, int width, int height, spi_host_device_t spihost = SPI3_HOST)
```

因此项目中建议定义为：

| 功能 | GPIO |
|---|---:|
| RLCD MOSI | GPIO12 |
| RLCD SCLK | GPIO11 |
| RLCD DC | GPIO5 |
| RLCD CS | GPIO40 |
| RLCD RST | GPIO41 |
| SPI Host | SPI3_HOST |
| 逻辑横屏宽度 | 400 |
| 逻辑横屏高度 | 300 |

注意：官方宣传分辨率为 `300 × 400`，而示例代码常以横屏方式使用 `400 × 300`。项目内部建议统一按横屏 UI：

```cpp
constexpr int RlcdWidth  = 400;
constexpr int RlcdHeight = 300;
```

### 2.2 I2C：RTC / SHTC3

Waveshare SHTC3 示例中：

```cpp
I2cMasterBus I2cbus(14, 13, 0);
```

`I2cMasterBus` 构造函数声明为：

```cpp
I2cMasterBus(int scl_pin, int sda_pin, int i2c_port);
```

因此：

| 功能 | GPIO |
|---|---:|
| I2C SCL | GPIO14 |
| I2C SDA | GPIO13 |

Arduino `Wire.begin()` 的参数顺序是：

```cpp
Wire.begin(SDA, SCL);
```

所以在 Arduino / PlatformIO 中应写：

```cpp
Wire.begin(13, 14);
```

建议定义：

```cpp
constexpr int I2cSda = 13;
constexpr int I2cScl = 14;
```

### 2.3 电池 ADC

官方 ADC 示例使用：

```cpp
ADC_CHANNEL_3
```

在 ESP32-S3 上，ADC1 Channel 3 对应 GPIO4。

电池电压读取逻辑：

```cpp
Vbat = Vadc * 3.0
```

原因：板载电池电压检测电路已经做了约 1/3 分压。

建议定义：

```cpp
constexpr int BatteryAdcPin = 4;
constexpr float BatteryDividerRatio = 3.0f;
```

### 2.4 建议的 `BoardConfig.h`

```cpp
#pragma once

namespace BoardConfig {

// Display: ST7305 RLCD
constexpr int RlcdMosi = 12;
constexpr int RlcdSclk = 11;
constexpr int RlcdDc   = 5;
constexpr int RlcdCs   = 40;
constexpr int RlcdRst  = 41;

constexpr int RlcdWidth  = 400;
constexpr int RlcdHeight = 300;

// I2C: PCF85063 RTC + SHTC3
constexpr int I2cSda = 13;
constexpr int I2cScl = 14;

// Battery ADC
constexpr int BatteryAdcPin = 4;
constexpr float BatteryDividerRatio = 3.0f;

}  // namespace BoardConfig
```

---

## 3. RLCD 与电子墨水屏的区别

### 3.1 这块屏不是墨水屏

这块屏幕是 **RLCD，全反射液晶屏**，不是 E-Ink / 电子墨水屏。

核心区别：

| 项目 | RLCD 反射 LCD | E-Ink 墨水屏 |
|---|---|---|
| 背光 | 无背光，靠环境光 | 无背光，靠环境光 |
| 刷新方式 | 类似 LCD 更新像素 | 电泳粒子迁移 |
| 刷新观感 | 通常不需要黑白闪屏 | 常见全屏闪烁 / 反相刷新 |
| 刷新速度 | 较快 | 较慢 |
| 残影 | 通常少于墨水屏 | 容易残影，需要全刷清理 |
| 断电保持 | 不支持 | 支持 |
| 动态内容 | 更适合 | 不适合频繁变化 |

### 3.2 对桌面信息站的意义

RLCD 很适合显示：

- 时间
- 日期
- 天气
- 温湿度
- 电池状态
- 轻量消息通知
- 简单图标
- 低频更新 UI

它比墨水屏更适合桌面信息站，因为时间、天气、温度、消息都是会变化的内容，不希望每次刷新都黑白闪一下。

### 3.3 重要限制

RLCD 不能像墨水屏那样断电保持画面。

正确理解：

```text
电子墨水屏：刷新完后，断电画面仍可保持
RLCD：刷新完后，屏幕驱动仍需要供电维持显示
```

因此，ESP32 可以 deep sleep，但屏幕供电和 ST7305 显示状态最好保持。不要在睡前随便：

```text
切断屏幕供电
拉低 RST
发送 display off
发送 sleep in
```

除非明确知道恢复流程和显示效果。

---

## 4. 电池与续航估算

### 4.1 3500mAh 18650 的粗略续航

理论计算公式：

```text
续航小时 ≈ 可用容量 mAh / 平均电流 mA
```

考虑电源转换损耗、截止电压、电池标称容量虚高等因素，3500mAh 电池实际可用容量建议先按：

```text
2800mAh ~ 3150mAh
```

估算表：

| 工作方式 | 估计平均电流 | 估计续航 |
|---|---:|---:|
| Wi-Fi 常在线、CPU 常跑、频繁刷新 | 100~160mA | 约 18~32 小时 |
| Wi-Fi 省电、低频刷新、CPU 降频 | 60~100mA | 约 28~52 小时 |
| 每分钟醒来，天气/消息低频同步 | 15~40mA | 约 3~9 天 |
| deep sleep 为主，5~15 分钟轮询 | 3~15mA | 约 8~40 天，取决于开发板外围漏电 |
| 极限优化 | <1~5mA | 可能数周以上，但需要实测 |

注意：实际续航必须通过 USB 电流表 / 万用表 / INA219 / INA226 实测，不能只靠理论。

### 4.2 电池电压读取

推荐封装：

```cpp
class BatteryMonitor {
public:
  bool begin();
  float readVoltage();
  int estimatePercent();
};
```

基础读取代码：

```cpp
float readBatteryVoltage() {
  constexpr int samples = 32;
  uint32_t sum_mv = 0;

  for (int i = 0; i < samples; ++i) {
    sum_mv += analogReadMilliVolts(BoardConfig::BatteryAdcPin);
    delay(2);
  }

  float adc_mv = sum_mv / static_cast<float>(samples);
  return adc_mv / 1000.0f * BoardConfig::BatteryDividerRatio;
}
```

初始化：

```cpp
analogReadResolution(12);
analogSetPinAttenuation(BoardConfig::BatteryAdcPin, ADC_12db);
```

### 4.3 电池百分比不要线性计算

不要简单使用：

```cpp
percent = (voltage - 3.0) / (4.2 - 3.0) * 100;
```

18650 / 锂电池放电曲线不是线性的。

建议使用查表法：

```cpp
int batteryPercentFromVoltage(float v) {
  struct Point {
    float voltage;
    int percent;
  };

  static constexpr Point table[] = {
    {4.20f, 100},
    {4.10f, 90},
    {4.00f, 75},
    {3.90f, 60},
    {3.80f, 45},
    {3.70f, 30},
    {3.60f, 20},
    {3.50f, 10},
    {3.40f, 5},
    {3.30f, 0},
  };

  if (v >= table[0].voltage) return 100;
  if (v <= table[sizeof(table) / sizeof(table[0]) - 1].voltage) return 0;

  for (size_t i = 0; i < sizeof(table) / sizeof(table[0]) - 1; ++i) {
    if (v <= table[i].voltage && v >= table[i + 1].voltage) {
      float ratio = (v - table[i + 1].voltage) /
                    (table[i].voltage - table[i + 1].voltage);
      return table[i + 1].percent +
             static_cast<int>(ratio * (table[i].percent - table[i + 1].percent));
    }
  }

  return 0;
}
```

### 4.4 UI 上建议显示

不要显示过于精确的剩余时间，例如：

```text
剩余 17 小时 23 分钟
```

更推荐：

```text
电池：3.91V / 约 60%
预计：2~3 天
```

或者：

```text
电量状态：充足 / 正常 / 偏低 / 请充电
```

---

## 5. 消息提醒与低功耗通信策略

### 5.1 ESP32 deep sleep 不能接收真正推送

ESP32-S3 进入 deep sleep 后：

```text
CPU 关闭
Wi-Fi 关闭
TCP / MQTT / WebSocket 连接断开
无法接收服务端主动推送
```

所以它不能像手机 APNs 那样保持系统级低功耗推送通道。

### 5.2 三种消息方案

#### 方案 A：deep sleep + 定时轮询

最适合电池供电。

流程：

```text
醒来
连接 Wi-Fi
请求 /api/device/{id}/sync
拿时间 / 天气 / 消息 / next_poll_seconds
刷新屏幕
关闭 Wi-Fi
进入 deep sleep
```

优点：

- 省电
- 实现简单
- 后台逻辑可控

缺点：

- 消息不实时
- 最坏延迟约等于轮询周期

#### 方案 B：modem-sleep / light sleep + MQTT

适合需要较低延迟但还能接受较高功耗的场景。

特点：

```text
Wi-Fi 保持关联
MQTT 订阅设备主题
ESP32 周期性醒来处理 Wi-Fi 任务
```

优点：

- 秒级到十几秒级延迟
- 体验接近推送

缺点：

- 平均电流显著高于 deep sleep
- 续航通常只有几天级，视 AP DTIM、信号质量、心跳策略而定

#### 方案 C：USB 供电实时，电池供电轮询

最推荐。

策略：

```text
USB 供电：MQTT / WebSocket 常在线
电池供电：deep sleep + 定时 HTTP 轮询
低电量：降低轮询频率
用户刚交互后：短时间保持在线
```

### 5.3 推荐后台接口

第一版建议用 HTTP 轮询，不要直接上 MQTT。

```http
GET /api/device/{device_id}/sync
```

返回：

```json
{
  "server_time": 1710000000,
  "weather": {
    "temp": 23.5,
    "humidity": 61,
    "text": "Cloudy"
  },
  "messages": [
    {
      "id": 123,
      "title": "提醒",
      "content": "该喝水了",
      "level": "normal",
      "created_at": 1710000000,
      "expire_at": 1710003600
    }
  ],
  "next_poll_seconds": 300
}
```

设备显示后调用：

```http
POST /api/device/{device_id}/messages/{message_id}/ack
```

服务器可通过 `next_poll_seconds` 动态控制设备功耗：

```text
无消息：300~900 秒
有未读消息：30~60 秒
夜间：1800~3600 秒
低电量：设备本地覆盖为更长间隔
```

### 5.4 推荐状态机

```cpp
enum class PowerMode {
  UsbPoweredOnline,
  BatteryPolling,
  BatteryLowPower,
};

struct SyncPolicy {
  uint32_t normalPollSeconds;
  uint32_t urgentPollSeconds;
  uint32_t nightPollSeconds;
  uint32_t lowBatteryPollSeconds;
};
```

典型策略：

| 状态 | 策略 |
|---|---|
| USB 供电 | MQTT/WebSocket 常在线 |
| 电池正常 | 5~15 分钟 HTTP 轮询 |
| 收到消息后 | 10 分钟内缩短到 30~60 秒轮询 |
| 夜间 | 30~60 分钟轮询 |
| 电量 < 20% | 30~60 分钟轮询 |
| 电量 < 10% | 仅 RTC / 温湿度，停止天气同步 |

---

## 6. 显示驱动策略

### 6.1 不建议业务代码直接使用官方 `DisplayPort`

Waveshare 示例里的 `DisplayPort` 可以作为底层参考，但不要让业务代码直接调用：

```cpp
RlcdPort.RLCD_SetPixel(...);
RlcdPort.RLCD_Display();
```

应该封装为：

```cpp
class RlcdDisplay {
public:
  bool begin();

  void clear(bool white = true);
  void setPixel(int x, int y, bool black);
  void drawLine(int x0, int y0, int x1, int y1, bool black);
  void drawRect(int x, int y, int w, int h, bool black);
  void fillRect(int x, int y, int w, int h, bool black);
  void drawBitmap(int x, int y, const uint8_t* bitmap, int w, int h);
  void drawText(int x, int y, const char* text);

  void flushFull();

  // 后续实验性加入
  void enterLowPowerDisplayMode();
  void exitLowPowerDisplayMode();
};
```

### 6.2 显示 framebuffer

屏幕像素：

```text
400 × 300 = 120000 pixels
```

如果按 1bit framebuffer：

```text
120000 / 8 = 15000 bytes
```

15KB 对 ESP32-S3 来说很轻，可以放 DRAM，也可以放 PSRAM。

Waveshare 示例使用 PSRAM 分配显示缓冲区，并且在查表优化模式下额外分配像素索引 LUT 和 bit mask LUT。对于第一版项目，如果代码复杂度优先，可以保留官方 LUT 方案；如果想节省 PSRAM，可以后续再优化。

### 6.3 刷新策略

第一版可以先整屏刷新：

```cpp
display.flushFull();
```

等功能稳定后，再考虑局部刷新：

```cpp
display.flushRect(x, y, w, h);
```

但要注意：Waveshare 官方 LVGL 示例虽然接收了 LVGL dirty area，但最终仍调用整屏 `RLCD_Display()`。这说明官方示例没有真正做好低功耗局部刷新。

第一版可接受整屏刷新，因为 400×300 黑白数据仅约 15KB。后续再根据功耗测量决定是否实现局部刷新。

### 6.4 睡眠前显示处理

如果想让 ESP32 deep sleep 时屏幕继续显示静态内容，应遵循：

```text
先刷新完画面
不要清屏
不要 reset 屏幕
不要切断屏幕供电
不要随意 display off / sleep in
如需低功耗显示，单独实验 ST7305 的低功耗命令
再让 ESP32 deep sleep
```

待验证项：

- ST7305 是否可在 ESP32 deep sleep 期间稳定保持画面
- 屏幕显示保持时的实际电流
- 发送低功耗命令后对显示对比度和刷新稳定性的影响
- 醒来后是否需要重新初始化屏幕

---

## 7. UI 架构建议

### 7.1 第一版不建议使用 LVGL

理由：

- 当前需求主要是信息展示，不是复杂 HMI
- 黑白 RLCD 不需要复杂动画、阴影、透明、渐变
- LVGL 的 RGB565 渲染到黑白屏会带来额外转换
- 官方 LVGL 示例没有真正优化局部刷新
- 低功耗状态机会比手写 UI 更难控制

第一版建议：

```text
RlcdDisplay
+ UiRenderer
+ 手写 Widget
```

### 7.2 手写 UI 分层

推荐：

```text
AppState
  ↓
UiRenderer
  ↓
Widgets
  ↓
RlcdDisplay
  ↓
ST7305 / SPI
```

示例结构：

```cpp
struct AppState {
  String timeText;
  String dateText;
  float indoorTemp;
  float indoorHumidity;
  float batteryVoltage;
  int batteryPercent;
  bool wifiConnected;
  String weatherText;
  float outdoorTemp;
  int unreadMessageCount;
  String latestMessageTitle;
};
```

```cpp
class UiRenderer {
public:
  void begin(RlcdDisplay& display);
  void drawHome(const AppState& state);
  void drawMessagePopup(const Message& message);
  void drawLowBattery(float voltage, int percent);
};
```

### 7.3 推荐首页布局

横屏 400×300：

```text
┌────────────────────────────────────────┐
│ 10:42              WiFi  Bat 78%       │
│ Sunday, 2026-05-10                     │
├────────────────────────────────────────┤
│                                        │
│              24°C  Cloudy              │
│                                        │
│  Indoor  26.3°C       Humidity 61%     │
│                                        │
│  Next: 18:30 喝水                       │
│                                        │
└────────────────────────────────────────┘
```

### 7.4 刷新频率建议

| 内容 | 建议刷新频率 |
|---|---:|
| 时间 | 每分钟刷新，不显示秒 |
| 室内温湿度 | 30~60 秒 |
| 天气 | 10~30 分钟 |
| 电池 | 1~5 分钟 |
| 消息 | 同步后立即刷新 |
| Wi-Fi 状态 | 状态变化时刷新 |

不要做高频动画，不要每秒全屏刷新。

---

## 8. LVGL 的定位

### 8.1 LVGL 类似什么

LVGL 可以理解为：

```text
嵌入式 UI 运行时 + 控件库 + 样式系统 + 布局系统 + 事件系统 + 渲染调度
```

前端类比：

| 前端概念 | LVGL 对应 |
|---|---|
| 组件库 | LVGL widgets |
| CSS | LVGL style |
| Flex/Grid | LVGL layout |
| click/input 事件 | LVGL event callback |
| 页面路由 | LVGL screen load |
| Canvas 绘制 | display flush callback |

### 8.2 LVGL 能帮什么

- Button / Label / Image / List / Table / Chart 等控件
- 页面切换
- 样式复用
- 事件处理
- 菜单、设置页、消息列表
- 编码器 / 触摸 / 按键输入适配

### 8.3 LVGL 不能帮什么

- 不能替代 ST7305 驱动
- 不能自动低功耗
- 不能接收后台推送
- 不能替代业务状态机
- 不能自动解决黑白屏刷新策略

### 8.4 什么时候上 LVGL

建议：

| 场景 | 是否建议 LVGL |
|---|---|
| 只有首页信息展示 | 不建议 |
| 时间、天气、电池、温湿度 | 不建议 |
| 有消息弹窗 | 仍可手写 |
| 有多级设置菜单 | 可以考虑 |
| 有消息列表 / 滚动列表 | 建议 |
| 有触摸屏版本计划 | 建议 |
| 要复杂动画 | 可考虑，但不适合低功耗 |

### 8.5 如果后续上 LVGL，建议分层

```text
AppController
  ↓
UiManager
  ↓
HomeScreen / MessageScreen / SettingsScreen
  ↓
LVGL widgets
  ↓
LvglPort
  ↓
RlcdDisplay
```

不要让业务逻辑直接到处写：

```cpp
lv_label_set_text(...);
```

应该通过：

```cpp
ui.update(state);
```

---

## 9. PlatformIO 环境配置

### 9.1 `platformio.ini`

建议项目根目录使用：

```ini
[platformio]
default_envs = waveshare_esp32_s3_rlcd_42

[env:waveshare_esp32_s3_rlcd_42]
platform = platformio/espressif32
board = esp32-s3-devkitc-1
framework = arduino

monitor_speed = 115200
upload_speed = 921600
monitor_filters =
    time
    colorize
    esp32_exception_decoder

board_build.mcu = esp32s3
board_build.f_cpu = 240000000L
board_build.f_flash = 80000000L
board_build.flash_mode = qio
board_upload.flash_size = 16MB
board_build.partitions = partitions_16mb.csv
board_build.filesystem = littlefs

build_flags =
    -D ARDUINO_USB_MODE=1
    -D ARDUINO_USB_CDC_ON_BOOT=1
    -D BOARD_HAS_PSRAM
    -D CORE_DEBUG_LEVEL=3

    -D WAVESHARE_ESP32_S3_RLCD_42

    -D RLCD_MOSI=12
    -D RLCD_SCLK=11
    -D RLCD_DC=5
    -D RLCD_CS=40
    -D RLCD_RST=41
    -D RLCD_WIDTH=400
    -D RLCD_HEIGHT=300

    -D BOARD_I2C_SDA=13
    -D BOARD_I2C_SCL=14

    -D BOARD_BAT_ADC=4
    -D BOARD_BAT_DIVIDER=3.0

lib_deps =
    bblanchon/ArduinoJson@^7.4.0
```

### 9.2 `partitions_16mb.csv`

```csv
# Name,   Type, SubType, Offset,   Size,     Flags
nvs,      data, nvs,     0x9000,   0x5000,
otadata,  data, ota,     0xe000,   0x2000,
app0,     app,  ota_0,   0x10000,  0x500000,
app1,     app,  ota_1,   0x510000, 0x500000,
littlefs, data, littlefs,0xA10000, 0x5E0000,
coredump, data, coredump,0xFF0000, 0x10000,
```

含义：

```text
app0/app1: 各 5MB，支持 OTA
littlefs: 约 5.875MB，用于字体、图标、配置、缓存
coredump: 64KB
```

### 9.3 初始测试 `src/main.cpp`

```cpp
#include <Arduino.h>
#include <Wire.h>
#include "BoardConfig.h"

float readBatteryVoltage() {
  constexpr int samples = 32;
  uint32_t sum_mv = 0;

  for (int i = 0; i < samples; ++i) {
    sum_mv += analogReadMilliVolts(BoardConfig::BatteryAdcPin);
    delay(2);
  }

  float adc_mv = sum_mv / static_cast<float>(samples);
  return adc_mv / 1000.0f * BoardConfig::BatteryDividerRatio;
}

void scanI2C() {
  Serial.println("I2C scan start");

  for (uint8_t addr = 1; addr < 127; ++addr) {
    Wire.beginTransmission(addr);
    uint8_t err = Wire.endTransmission();

    if (err == 0) {
      Serial.printf("I2C device found: 0x%02X\n", addr);
    }
  }

  Serial.println("I2C scan done");
}

void setup() {
  Serial.begin(115200);
  delay(1500);

  Serial.println();
  Serial.println("ESP32-S3 RLCD 4.2 PlatformIO test");

  Serial.printf("PSRAM found: %s\n", psramFound() ? "yes" : "no");
  Serial.printf("Free heap: %u bytes\n", ESP.getFreeHeap());
  Serial.printf("Free PSRAM: %u bytes\n", ESP.getFreePsram());
  Serial.printf("Flash size: %u bytes\n", ESP.getFlashChipSize());

  Wire.begin(BoardConfig::I2cSda, BoardConfig::I2cScl);
  Wire.setClock(400000);

  analogReadResolution(12);
  analogSetPinAttenuation(BoardConfig::BatteryAdcPin, ADC_12db);

  scanI2C();
}

void loop() {
  float vbat = readBatteryVoltage();
  Serial.printf("Battery voltage: %.3f V\n", vbat);
  delay(1000);
}
```

---

## 10. 推荐项目结构

```text
project-root/
├── platformio.ini
├── partitions_16mb.csv
├── README.md
├── ESP32-S3-RLCD-4.2_Project_Handbook.md
│
├── include/
│   ├── BoardConfig.h
│   └── AppConfig.h
│
├── src/
│   └── main.cpp
│
├── lib/
│   ├── RlcdDisplay/
│   │   ├── RlcdDisplay.h
│   │   └── RlcdDisplay.cpp
│   │
│   ├── BatteryMonitor/
│   │   ├── BatteryMonitor.h
│   │   └── BatteryMonitor.cpp
│   │
│   ├── Shtc3Sensor/
│   │   ├── Shtc3Sensor.h
│   │   └── Shtc3Sensor.cpp
│   │
│   ├── RtcClock/
│   │   ├── RtcClock.h
│   │   └── RtcClock.cpp
│   │
│   └── UiRenderer/
│       ├── UiRenderer.h
│       ├── UiRenderer.cpp
│       ├── Widgets.h
│       └── Widgets.cpp
│
└── data/
    ├── icons/
    ├── fonts/
    └── config.json
```

### 10.1 各模块职责

| 模块 | 职责 |
|---|---|
| `BoardConfig` | 板级引脚、屏幕尺寸、分压比例 |
| `AppConfig` | 业务配置，如轮询间隔、城市、API URL |
| `RlcdDisplay` | ST7305 初始化、framebuffer、绘制、刷新 |
| `BatteryMonitor` | 电池电压读取、百分比估算、低电量判断 |
| `Shtc3Sensor` | 温湿度读取 |
| `RtcClock` | RTC 读写、NTP 同步后校准 RTC |
| `WifiManager` | Wi-Fi 连接、断开、重连策略 |
| `MessageClient` | 后台消息同步、ACK |
| `WeatherClient` | 天气 API 请求与解析 |
| `UiRenderer` | 首页、消息弹窗、低电量页绘制 |
| `PowerManager` | USB/电池模式判断、deep sleep 策略 |
| `AppController` | 业务状态机 |

---

## 11. Codex 开发约束

### 11.1 总原则

Codex 应该遵守：

```text
小步修改
先编译通过
每次只封装一个模块
不要一次性生成完整项目
不要把业务逻辑写进驱动层
不要把驱动细节暴露给业务层
不要引入未经确认的大型依赖
```

### 11.2 编码风格

建议：

- 使用 C++17 能力，但避免复杂模板
- 类职责单一
- 头文件只暴露必要接口
- 不要在头文件里写大量实现
- 不要使用异常
- Arduino / ESP32 项目中优先返回 `bool` 表示初始化成功与否
- 底层驱动日志统一使用 `Serial.printf` 或 ESP_LOG，项目后期再统一日志封装
- 所有 GPIO 放在 `BoardConfig.h`
- 所有业务配置放在 `AppConfig.h`

### 11.3 不希望 Codex 做的事

不要：

```text
复制整个 Waveshare 示例工程结构
把所有代码写进 main.cpp
直接引入 LVGL 并重构全部 UI
随意改 SPI/I2C/ADC 引脚
在驱动构造函数里做大量硬件初始化
在析构函数里留空但实际分配资源
业务代码里直接调用 RLCD_SendCommand / RLCD_SendData
使用 delay 写复杂状态机
```

### 11.4 推荐给 Codex 的任务拆分

#### 任务 1：封装 `RlcdDisplay`

提示词：

```text
基于 Waveshare ESP32-S3-RLCD-4.2 官方 display_bsp.cpp/display_bsp.h，重构一个 RlcdDisplay 类。
要求：
1. 保留 ST7305 初始化序列；
2. 使用 BoardConfig.h 中的引脚；
3. 提供 begin/clear/setPixel/drawLine/drawRect/fillRect/drawBitmap/flushFull；
4. 不引入 LVGL；
5. 不让业务层直接调用 RLCD_* 原始函数；
6. 代码可在 PlatformIO Arduino framework 下编译；
7. 先实现整屏刷新，不做局部刷新。
```

#### 任务 2：封装 `BatteryMonitor`

```text
实现 BatteryMonitor 类。
要求：
1. 使用 GPIO4 读取电池 ADC；
2. 使用 analogReadMilliVolts；
3. 电压 = ADC 电压 * 3.0；
4. 多次采样求平均；
5. 百分比用查表法，不要线性映射；
6. 提供 readVoltage/estimatePercent/isLow/isCritical。
```

#### 任务 3：封装 `UiRenderer`

```text
实现一个不依赖 LVGL 的 UiRenderer。
要求：
1. 使用 RlcdDisplay 的绘图 API；
2. 横屏 400x300；
3. 绘制状态栏、时间、日期、天气、室内温湿度、电池、消息摘要；
4. UI 使用黑白高对比风格；
5. 不要动画；
6. 不要每秒刷新；
7. drawHome 接收 AppState。
```

#### 任务 4：实现同步状态机

```text
实现 AppController 初版。
要求：
1. 启动后读取电池、温湿度、RTC；
2. 连接 Wi-Fi；
3. 调用 HTTP sync API；
4. 更新 AppState；
5. 刷新 UI；
6. 根据 next_poll_seconds 进入 deep sleep；
7. 不要使用阻塞式复杂 delay；
8. 网络失败时显示离线状态并降低重试频率。
```

---

## 12. 第一版里程碑

### Milestone 1：环境验证

目标：

- PlatformIO 能编译 / 上传
- 串口正常
- PSRAM found: yes
- Flash size 约 16MB
- I2C 扫描能发现 RTC / SHTC3
- 电池电压可读取

### Milestone 2：屏幕点亮

目标：

- 封装 `RlcdDisplay`
- 能清屏
- 能画像素 / 线 / 矩形
- 能显示简单英文文本
- 能整屏刷新

### Milestone 3：首页 UI

目标：

- 显示时间
- 显示电池电压 / 百分比
- 显示温湿度
- 显示 Wi-Fi 状态
- 显示天气占位数据

### Milestone 4：联网同步

目标：

- 连接 Wi-Fi
- 请求 `/sync`
- 解析 JSON
- 显示天气和消息
- 支持 ACK 消息

### Milestone 5：低功耗轮询

目标：

- 刷新后进入 deep sleep
- RTC timer 唤醒
- 醒来后重新连接 Wi-Fi
- 按服务器 `next_poll_seconds` 调整睡眠时间
- 低电量时自动延长间隔

### Milestone 6：实测功耗

目标：

测量：

- deep sleep + 屏幕保持显示电流
- Wi-Fi 连接过程峰值电流
- HTTP 同步平均耗时和电流
- 整屏刷新电流和耗时
- 一轮完整 sync + display + sleep 的平均能耗

---

## 13. 待验证清单

这些点不要让 Codex 直接假设成立，需要实测：

### 13.1 显示相关

- ESP32 deep sleep 时，RLCD 是否稳定保持显示
- ST7305 低功耗显示命令是否影响对比度
- 是否需要定期刷新以防显示异常
- 是否支持可靠局部刷新
- 醒来后是否必须重新 `RLCD_Init()`
- 屏幕长时间显示静态内容是否有残影或对比度变化

### 13.2 电源相关

- 18650 供电时开发板 deep sleep 实际电流
- 屏幕保持显示时的额外电流
- 音频芯片 / SD 卡 / 麦克风是否有额外静态功耗
- 电源指示灯是否影响续航
- USB 供电与电池供电能否可靠识别

### 13.3 网络相关

- Wi-Fi 重连耗时
- TLS 握手耗时
- HTTP 请求耗时
- 信号差时平均耗电增加多少
- 轮询间隔 1/5/10/30 分钟下的续航差异

### 13.4 软件相关

- PlatformIO 当前 espressif32 平台对应 Arduino-ESP32 版本
- `ADC_12db` 在所用版本中是否可用；如果不可用，改为当前版本支持的枚举
- PSRAM 是否默认可用
- LittleFS 是否可正常挂载
- 字体/图标存储放 LittleFS 还是编译进固件

---

## 14. 资料来源

主要参考：

1. Waveshare 文档：`https://docs.waveshare.com/ESP32-S3-RLCD-4.2`
2. 微雪中文文档：`https://docs.waveshare.net/ESP32-S3-RLCD-4.2`
3. Waveshare 产品页：`https://www.waveshare.net/shop/ESP32-S3-RLCD-4.2.htm`
4. Waveshare GitHub：`https://github.com/waveshareteam/ESP32-S3-RLCD-4.2`
5. Waveshare ST7305 示例：`02_Example/Arduino/09_LVGL_V9_Test/display_bsp.cpp`
6. Waveshare ADC 示例：`02_Example/Arduino/03_ADC_Test/adc_bsp.cpp`
7. Waveshare SHTC3 示例：`02_Example/Arduino/05_I2C_SHTC3`

---

## 15. 给后续开发的最终建议

优先级如下：

```text
1. 先跑通 PlatformIO 环境
2. 先验证 ADC / I2C / PSRAM / 串口
3. 再封装 RlcdDisplay
4. 再做首页 UI
5. 再做 HTTP sync
6. 再做 deep sleep
7. 最后再考虑 LVGL / MQTT / 局部刷新 / OTA
```

不要一开始追求“像手机一样实时推送”。这块设备更适合：

```text
插电：实时在线
电池：低功耗轮询
```

第一版目标应该是稳定、能显示、能同步、能睡眠，而不是一次做到极致。

