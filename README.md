# esp32clock

A clock for the **Espressif ESP32-P4-Function-EV-Board** (with the 7-inch 1024×600 HMI sub-board)

- Four clock faces, swipe left/right to switch (choice is remembered): Flip
  (Gluqlo-style animated digits), Digital, Classic (analog) and Diver (analog,
  luminous markers, 60-minute bezel, date window).
- Wi-Fi 6 through the on-board ESP32-C6 (ESP-Hosted), SNTP time sync.
- Long-press the clock for Settings: Wi-Fi scan/connect with an on-screen
  keyboard, saved networks; Display (brightness 5-100%, night auto-dim
  22:00-07:00 capped at 10%, clock face); Time zone (12 fixed-offset zones,
  default UTC+8, no daylight saving).


## 1. Hardware

硬件选型和方案

| Item | Value |
| --- | --- |
| Board | ESP32-P4-Function-EV-Board V1.6 (chip rev v3.1) |
| Screen | ESP32-P4-HMI-SubBoard, 7" 1024×600, EK79007 + GT911 touch |
| Wiring | FPC cable **plus two jumpers**: sub-board `RST_LCD` → J1 `GPIO27`, `PWM` → J1 `GPIO26`. Without them the screen stays black. |
| UI 框架 | LVGL 9 |
| Wi-Fi | 板载 ESP32-C6,经 ESP-Hosted(SDIO) |
| 固件架构 | 普通单应用,`idf.py` 构建烧录 |
| ESP-IDF | v6.1.1 |


## 2. 开发流程

### 2.1 选 BSP

用 ESP Component Registry 查到官方 BSP:`espressif/esp32_p4_function_ev_board`(v5.2.3,
要求 ESP-IDF ≥ 5.4)。它集成了:

- 显示:`ek79007`(1024×600)、`ili9881c`(800×1280)、`lt8912b`(HDMI)
- 触摸:GT911
- 音频、SD 卡、USB、摄像头

BSP 不含 Wi-Fi:P4 没有片上无线,Wi-Fi 6 来自外挂 ESP32-C6,需另外接入 `esp_hosted`。

### 2.2 验证硬件:先跑 BSP 官方示例

```sh
idf.py create-project-from-example "espressif/esp32_p4_function_ev_board:display"
idf.py set-target esp32p4
idf.py build flash monitor
```

这一步先确认工具链、BSP 与板子本身没问题,再动应用代码。

### 2.3 屏幕连接

ESP32-P4-HMI-SubBoard(LCD 转接板)上的 `RST_LCD` 和 `PWM` 信号引到排针, **必须用杜邦线接到主板**:

- `RST_LCD` → 主板 J1 的 `GPIO27`(面板复位)
- `PWM` → 主板 J1 的 `GPIO26`(背光 PWM)

不接线时面板得不到复位,背光 PWM 悬空,所以"初始化全部成功却不显示"。接线后红绿蓝白正常轮换。


### 2.4 移植 Wi-Fi 与 SNTP

`net/wifi_service.c` 基于标准 `esp_wifi` / `esp_netif` API,
(扫描、连接、保存最多 16 个网络到 NVS、断线重连、SNTP):

- 加入依赖 `espressif/esp_wifi_remote`,它会拉取 `esp_hosted`;
  默认 SDIO 引脚与 Function EV Board 一致(CLK18 CMD19 D0–D3 14–17,复位 GPIO54)。
- 校时前,时钟页底部显示状态:Connecting / Synchronizing / 引导长按进入设置。

### 2.5 设置界面

用 LVGL 实现(`settings/settings_ui.c`):

- **长按**时钟进入设置;菜单含 Wi-Fi 与 Brightness。
- **Wi-Fi 页:** 扫描列表(信号强度、Saved / Connected / Open / Password required)、
  Refresh、点击网络连接。
- **密码页:** 密码模式输入框 + 屏幕键盘,8–63 位或 64 位十六进制,离开页面即清空。
- **亮度页:** 5%–100% 滑块,松手或点 Back 时写入 NVS,重启后保持。

### 2.6 其他调整

- 固件超过默认 1 MB 分区,改为 16 MB 闪存 + `SINGLE_APP_LARGE` 分区表(应用分区 0x177000)。
- 启用 LVGL 字体 Montserrat 20/24/28/32,默认 14 号字在 1024×600 上太小。


## 3. Build and flash

Requires ESP-IDF v6.1.1 (v5.4+ should work).

```sh
source ~/.espressif/tools/activate_idf_v6.1.1.sh   # 按你的安装路径
idf.py set-target esp32p4
idf.py menuconfig          # 可选: esp32 clock -> initial Wi-Fi SSID/password
idf.py -p <PORT> flash monitor
```

Use the board's **UART** USB-C port. Wi-Fi can also be chosen on the device
(long-press → Wi-Fi). Do not commit `sdkconfig`: it may contain your Wi-Fi password.

- 使用板子标 **UART** 的 USB-C 口。macOS 上串口形如 `/dev/cu.usbmodemXXXX`。
- 连不上时:按住 BOOT 再按 RESET,然后松开 BOOT。
- `monitor` 占用串口时无法烧录,先按 `Ctrl+]` 退出。
- 板子 ≤ V1.4(芯片版本 < v3.0)需在 `sdkconfig.defaults` 加
  `CONFIG_ESP32P4_SELECTS_REV_LESS_V3=y`。
- 本移植在 V1.6 板子上验证。


## 4. 已知限制与后续

- 没有自动旋转与休眠;时区固定为 UTC+8。
- 固件约 1.3 MB,应用分区剩余约 10%,继续加功能前需放大分区。
- 编译有 LVGL 弃用警告(`lv_list`、`lv_obj_add_flag` 等),不影响功能。
- `sdkconfig` 可能包含 menuconfig 设置的 Wi-Fi 密码,已加入 `.gitignore`,**不要提交**。
- 标注"适用于 Function EV Board V1.2/1.3"的 HMI 子板,在 V1.6 主板上实测可用。
