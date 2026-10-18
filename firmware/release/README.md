# 固件下载

- 当前 OTA 文件：[case_display-3.3.0-ota.bin](case_display-3.3.0-ota.bin)
- [大小与 SHA-256 清单](case_display-3.3.0-ota.bin.json)

已经安装支持本项目 OTA 布局的版本时，只需在设置网页上传应用 `.bin`，无需使用下面的首次烧录文件。

## 首次通过 USB 安装

适用于本项目 ESP32-S3-WROOM-1-N16R8（16 MB Flash、8 MB Octal PSRAM）与对应 ST7789 接线。请使用 ESP-IDF v6.1 环境中的 esptool 5.x，在本目录执行，替换 `COM5` 为实际串口：

```sh
python -m esptool --chip esp32s3 -p COM5 -b 460800 --before default-reset --after hard-reset write-flash --flash-mode dio --flash-size 16MB --flash-freq 80m 0x0 first-flash/bootloader.bin 0x8000 first-flash/partition-table.bin 0x10000 case_display-3.3.0-ota.bin 0x310000 first-flash/ota_data_initial.bin
```

这个命令写入引导程序、分区表、基础应用和初始 OTA 状态，用于第一次建立本项目布局。普通应用更新使用网页，避免反复重写基础应用和初始化启动选择。

首次安装文件由当前 ESP-IDF 构建输出提供。固件内不包含使用者的 Wi-Fi 密码或天气私钥，首次启动后通过手机设置。
