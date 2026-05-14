# SD 卡持久化配置

TinyPanel 可以使用 microSD 卡保存运行时配置、缓存和日志。SD 卡不是必需的：
没有卡、文件缺失或文件格式错误时，固件会继续启动，并回退到编译进固件的默认配置。

## 目录结构

启动时，固件会自动创建下面的目录：

```text
/tinypanel/
  config/
  cache/
  logs/
  calib/
  state/
```

下面所有路径都相对于 SD 卡根目录。

## WiFi 配置

路径：

```text
/tinypanel/config/wifi.json
```

示例：

```json
{
  "networks": [
    { "ssid": "HomeWiFi", "password": "password1" },
    { "ssid": "PhoneHotspot", "password": "password2" }
  ]
}
```

行为：

- 固件会优先读取 SD 卡里的 WiFi 配置。
- 如果 `wifi.json` 不存在或格式错误，会回退到 `include/AppSecrets.h`。
- 最多加载 5 个 WiFi 网络。
- 密码会以明文形式保存在 SD 卡上。

## 设备配置

路径：

```text
/tinypanel/config/device.json
```

示例：

```json
{
  "device_id": "tinypanel-001",
  "timezone": "CST-8",
  "message_channel": "desk",
  "hub_message_limit": 10,
  "hub_todo_limit": 12,
  "hub_telemetry_s": 300,
  "hub_message_poll_s": 60,
  "hub_weather_poll_s": 600,
  "hub_todo_poll_s": 60,
  "battery_log_interval_s": 60
}
```

行为：

- 文件缺失或格式错误时使用固件内置配置。
- `device_id` 会同时用于 Hub 客户端和遥测。
- `timezone` 用于 NTP 同步。
- `message_channel` 用于消息订阅。
- `hub_message_limit` 最大为 10，`hub_todo_limit` 最大为 12。
- 所有 `*_s` 字段单位都是秒。

## 消息缓存

路径：

```text
/tinypanel/cache/messages.json
```

固件收到新的 Hub 消息后会写入这个文件。下次开机时会先恢复缓存消息，
这样离线或网络还没连上时，MESSAGE 页面也不会是空的。

示例：

```json
{
  "version": 1,
  "messages": [
    {
      "id": 123,
      "channel": "desk",
      "author": "server",
      "body": "hello",
      "created_at": "2026-05-14T19:30:00+08:00"
    }
  ]
}
```

## 电池曲线

路径：

```text
/tinypanel/calib/battery_curve.csv
```

示例：

```csv
raw_adc,percent
1655,100.0
1650,99.9
1645,99.77
1175,0.0
```

规则：

- `raw_adc` 必须按从大到小排列。
- `percent` 必须在 `0.0` 到 `100.0` 之间。
- 至少需要 2 个有效点。
- 如果解析失败，固件会使用内置电池曲线。

## 电池日志

路径：

```text
/tinypanel/logs/battery_YYYYMMDD.csv
```

如果 RTC 时间还不可用，会使用备用路径：

```text
/tinypanel/logs/battery_uptime.csv
```

表头：

```csv
timestamp,uptime_s,raw_adc,raw_voltage_mv,voltage,percent,charging
```

当 SD 存储可用时，固件大约每分钟追加一条电池采样记录。

## 编译内置配置回退

没有 SD 卡配置时，仍然可以使用 `include/AppSecrets.h`：

```cpp
namespace AppSecrets {

constexpr const char* WifiSsid = "YOUR_WIFI_SSID";
constexpr const char* WifiPassword = "YOUR_WIFI_PASSWORD";

constexpr const char* HubServerBaseURL = "http://192.168.1.2:8080/api/v1";
constexpr const char* HubServerApiKey = "YOUR_HUB_SERVER_API_KEY";

}  // namespace AppSecrets
```

建议把 Hub API Key 继续放在 `AppSecrets.h` 里。除非你明确接受风险，
否则不要把服务器密钥放到可拔出的 SD 卡上。
