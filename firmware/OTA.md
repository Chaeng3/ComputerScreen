# Wi-Fi 固件升级

当前对外版本为 **3.3**，固件内标识 `3.3.1-ota`；此前 1.x 为开发阶段编号。

安装 OTA 版本后，日常固件更新通过手机或电脑网页完成，无需按键或拆机连接 USB。

**已知问题：部分环境的热点 OTA 上传仍会超时或中断，稳定性待修复。**

## 旧名称迁移

从 3.3.0-ota 或更早的 ESP-IDF OTA 版本升级时，先上传 [ComputerScreen-3.3.1-ota-bridge.bin](release/ComputerScreen-3.3.1-ota-bridge.bin)，重启后再上传 [正式包](release/ComputerScreen-3.3.1-ota.bin)。旧版按 `case_display` 校验应用名，过渡包保留该内部标识，同时允许后续升级到 ComputerScreen。热点、页面和主机名在过渡包中也已更新。

设置热点改为 `ComputerScreen-XXXX`，密码改为 `ComputerScreenXXXX`，末四位不变；保存的路由器 Wi-Fi、城市和天气私钥保留。旧 NVS 存储名称仅作为内部兼容键保留，避免丢失已有设置。旧版 Arduino OLED 不适用此迁移流程。

## 操作

1. 手机与副屏连接同一网络，打开 `http://computerscreen.local/`；域名不通时从路由器设备列表查找 `computerscreen` 的 IP。
2. 或重新给副屏上电，连接屏幕对应的 `ComputerScreen-XXXX` 设置热点，使用屏幕显示的密码，在浏览器打开 `http://192.168.4.1/`。电脑与副屏处于不同网段时也可使用这种方法。
3. 在“固件升级”中选择本项目提供的 `ComputerScreen-版本号-ota.bin`，点击“上传并升级”。
4. 上传达到 100% 后继续等待镜像校验；成功后自动重启，约 30 秒后刷新页面确认版本。
5. 连接设置热点时，重启会短暂断网，必要时重新连接同名热点。

升级保留 Wi-Fi、城市、天气私钥、亮度和时段。不要上传 `bootloader.bin`、分区表、合并烧录镜像或 `.json` 清单。

## 失败处理

- 其他芯片、其他项目、非 OTA 旧版及非应用文件会被拒绝。
- 新镜像写入非运行分区，SDK 检查芯片、校验和及附加 SHA-256，验证通过后才切换启动分区。
- 上传中断或校验失败不切换启动项，原版本继续运行。浏览器异常时先重新连接并核对版本，再决定是否重传。
- 新版本先进入待确认状态；配置、屏幕、网络和网页初始化并运行后，设备探测自身网页服务，通过后确认启用。不依赖外部互联网、NTP 或天气服务来决定版本是否可用。
- 确认前发生重启，会回退到此前可启动的版本；自身网页服务持续异常且存在回退版本时，应用会主动回退。若新版本首次启动卡住，可重新上电触发待确认版本的回退。已确认后的所有程序错误及硬件故障不属于该机制能保证恢复的范围。

## 分区

| 分区 | 起始地址 | 大小 |
| --- | --- | --- |
| nvs | 0x9000 | 0x6000 |
| phy_init | 0xf000 | 0x1000 |
| factory | 0x10000 | 3 MB |
| otadata | 0x310000 | 8 KB |
| ota_0 | 0x320000 | 4 MB |
| ota_1 | 0x720000 | 4 MB |
| assets | 0xb20000 | 0x4e0000 |

普通 OTA 不覆盖基础应用，也不更新引导程序、分区表或预留资源分区。后续保持当前布局；改动这些底层内容需要专门迁移方案或 USB。构建时以最小的 3 MB 应用分区检查大小，网页接收上限为备用分区的 4 MB。

已启用 `CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE`，没有启用会写入 eFuse 的防降级机制。实现参考[ESP-IDF 官方 OTA 文档](https://docs.espressif.com/projects/esp-idf/zh_CN/stable/esp32s3/api-reference/system/ota.html)。

## 开发与验证

升级版本时修改根 CMakeLists 中的 `PROJECT_VER`，保留 `-ota` 后缀。用 ESP-IDF 构建后，上传 `build/ComputerScreen.bin`，或通过 `tools/export_ota.py` 导出命名后的文件。

已实测：正常 Wi-Fi 更新、缺失同源请求头、错误芯片、非 OTA 版本、512 字节后中断上传、完整上传损坏文件，以及新分区首次启动确认前复位后回退。这里的启动中断由串口 RTS 复位模拟，未进行真实断电写入测试。
