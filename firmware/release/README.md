# 固件下载

- 当前 OTA 文件：[ComputerScreen-3.3.2-ota.bin](ComputerScreen-3.3.2-ota.bin)
- [大小与 SHA-256 清单](ComputerScreen-3.3.2-ota.bin.json)

已经安装 ComputerScreen 固件时，只需在设置网页上传正式应用 `.bin`。

从 3.3.0-ota 或更早的 ESP-IDF OTA 固件迁移时，先上传 [过渡包](ComputerScreen-3.3.1-ota-bridge.bin)，重启后再上传当前正式包。过渡包保留旧项目名供旧固件验证，对外名称已统一为 ComputerScreen；图标居中调整包含在 3.3.2 正式包中。[过渡包校验清单](ComputerScreen-3.3.1-ota-bridge.bin.json)。旧 Arduino OLED 固件不能使用这些文件。

已知部分环境的热点 OTA 上传仍会超时或中断，本次更新不代表此问题已修复。

## 首次通过 USB 安装

适用于本项目 ESP32-S3-WROOM-1-N16R8（16 MB Flash、8 MB Octal PSRAM）与对应 ST7789 接线。请使用 ESP-IDF v6.1 环境中的 esptool 5.x，在本目录执行，替换 `COM5` 为实际串口：

```sh
python -m esptool --chip esp32s3 -p COM5 -b 460800 --before default-reset --after hard-reset write-flash --flash-mode dio --flash-size 16MB --flash-freq 80m 0x0 first-flash/bootloader.bin 0x8000 first-flash/partition-table.bin 0x10000 ComputerScreen-3.3.2-ota.bin 0x310000 first-flash/ota_data_initial.bin
```

这个命令写入引导程序、分区表、基础应用和初始 OTA 状态，用于第一次建立本项目布局。普通应用更新使用网页，避免反复重写基础应用和初始化启动选择。

首次安装文件由当前 ESP-IDF 构建输出提供。固件内不包含使用者的 Wi-Fi 密码或天气私钥，首次启动后通过手机设置。
