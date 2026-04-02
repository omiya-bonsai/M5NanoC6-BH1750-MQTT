# M5NanoC6 + BH1750 MQTT Publisher v4

[統合ハブ](https://github.com/omiya-bonsai/m5papers3-weather-learning-system)

## 概要

M5NanoC6 と M5Stack用環境光センサユニット（BH1750FVI-TR）を使い、
照度値・メタ情報・状態情報を MQTT で配信します。

今回追加した要素:
- `home/lux_status` の Last Will
- NTP 同期
- `unix_time` の付与
- `time_valid` フラグの付与

配信トピック:

```text
home/lux
home/lux_meta
home/lux_status
```

---

## 今回の変更点

### 1. Last Will を追加
マイコンが無言で落ちた場合、MQTT ブローカーが `home/lux_status` に
`offline` を retained publish します。

例:

```json
{
  "status": "offline",
  "reason": "last_will",
  "wifi": "unknown",
  "ip": "0.0.0.0",
  "sensor_ready": false,
  "sensor_error_count": 0,
  "wifi_reconnect_count": 0,
  "mqtt_reconnect_count": 0,
  "uptime_s": 0,
  "seq": 0,
  "unix_time": 0,
  "time_valid": false
}
```

これで「値が止まった」のか「マイコンが落ちた」のかを切り分けやすくなります。

### 2. NTP を追加
起動時と Wi-Fi 再接続後に NTP 同期します。

設定:
- `ntp.nict.jp`
- `pool.ntp.org`
- JST

### 3. `unix_time` を追加
publish する payload に Unix time を含めます。

### 4. `time_valid` を追加
NTP が有効かどうかを bool で出します。

---

## トピック仕様

### `home/lux`

v3 までは単純な数値でしたが、今回は時刻を含めるため JSON に変更しました。

例:

```json
{
  "lux": 1234.5,
  "unix_time": 1743476400,
  "time_valid": true
}
```

### `home/lux_meta`

例:

```json
{
  "lux": 1234.5,
  "avg": 1180.2,
  "delta": 54.3,
  "delta_prev": -20.5,
  "rate_pct": 4.60,
  "trend": "stable",
  "samples": 12,
  "interval_ms": 30000,
  "seq": 42,
  "unix_time": 1743476400,
  "time_valid": true
}
```

### `home/lux_status`

例:

```json
{
  "status": "ok",
  "reason": "boot",
  "wifi": "connected",
  "ip": "192.168.0.2",
  "sensor_ready": true,
  "sensor_error_count": 0,
  "wifi_reconnect_count": 1,
  "mqtt_reconnect_count": 1,
  "uptime_s": 8,
  "seq": 0,
  "unix_time": 1743476400,
  "time_valid": true
}
```

---

## 率直な注意点

今回、`home/lux` は JSON に変えています。  
これは意図的です。

理由:
- ユーザー要望が「トピックの中に unix time を入れたい」だった
- 単純な数値 payload のままだと時刻を持てない
- 後で M5PaperS3 側で扱うなら JSON の方が拡張しやすい

ただし、既存の `home/lux` 購読側が「数値だけ」を期待しているなら、そこは壊れます。  
つまり **互換性より実用性を優先した変更** です。

もし互換性を維持したいなら、
- `home/lux` は数値のまま
- `home/lux_meta` にだけ `unix_time`
という分離の方が安全です。

---

## 今回含めている機能

- `home/lux`
- `home/lux_meta`
- `home/lux_status`
- 移動平均
- 前回との差分
- 移動平均との差分
- 変化率
- トレンド判定
- 連番
- retained publish
- NTP 同期
- Unix time 付与
- time_valid
- Last Will offline 通知
- センサ失敗カウント
- Wi-Fi / MQTT 再接続カウント

---

## 配線

M5NanoC6 HY2.0-4P (Grove):

- SDA = G2
- SCL = G1

```cpp
Wire.begin(2, 1);
```

---

## 必要ライブラリ

Arduino IDE の Library Manager:

- `PubSubClient`
- `BH1750`

---

## 設定ファイル

`config.h`:

```cpp
static constexpr const char* WIFI_SSID = "YOUR_WIFI_SSID";
static constexpr const char* WIFI_PASSWORD = "YOUR_WIFI_PASSWORD";
static constexpr const char* MQTT_BROKER = "broker.local";
static constexpr uint16_t MQTT_PORT = 1883;
```

---

## 確認コマンド

```bash
mosquitto_sub -h broker.local -t "home/lux#" -v
```

---

## 次に確認すべきこと

1. 起動時に `home/lux_status` へ `boot` が出るか
2. `home/lux_status` に `unix_time` が入っているか
3. Wi-Fi を切ったときに復帰後 NTP が再同期されるか
4. 強制リセットや電断時に Last Will の `offline` が出るか
