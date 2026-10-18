# ESP32-S3 天气时钟固件

当前版本 **3.3.0-ota**，基于 ESP-IDF v6.1，适用 ESP32-S3-WROOM-1-N16R8 与 320×240 横向 ST7789 屏。

构建、接线、首次配网与功能介绍见[项目首页](../README.md)，[OTA 升级说明](OTA.md)及[下载目录](release/README.md)。

## 模块

- `board.h` / `display.c`：引脚、方向、屏幕缓冲及字库绘制。
- `clock_ui.c`：大时间、日期、左右天气区域和天气轮播。
- `lighting.c` / `lighting_plan.h`：分时亮度及渐变。
- `network.c`：AP/STA、Wi-Fi 扫描、自动重连、mDNS 和 NTP。
- `portal.c` / `portal.html` / `portal.js`：中文网页设置。
- `ota_update.c` / `ota.js`：固件上传、校验、启动确认与回退。
- `settings.c`：NVS 配置；网络设置保持 v1 的 312 字节布局，新增亮度配置单独保存，以兼容基础固件。
- `weather.c` / `weather_decode.c` / `weather_layout.c`：心知天气请求、解析、缓存与预报选择。

当前固件使用纯黑背景。`assets/themes/`、`scene.c` 与三份 RGB565 数据是停用的动漫试作，保留作参考，不参与当前构建。

## 天气与城市

实况约每 15 分钟更新，逐日预报约每小时更新。失败后退避，联网恢复后安排更新；跨日重新获取预报。数据访问范围取决于心知天气账号权限。

优先请求今天加未来三天；若接口只返回三天，则显示实际返回的今天、明天和后天，不把今天冒充未来一天。缓存过期时标记“待更新”。

内置中国大陆 31 个省级地区、371 个城市，使用心知天气官方城市表 2026-07-02 快照中的城市 ID；不包含市下辖区县。生成工具为 `tools/build_cities.py`，输入[官方城市清单](https://cdn.sencdn.com/download/data/thinkpage_cities.zip)中的 Excel。日常编译直接使用内置 JSON。

## 字库与开发工具

`font_cn16.bin` 包含基本汉字区、ASCII 和温度符号。`tools/build_font.py` 是 Windows 上的字库生成工具，使用系统安装的微软雅黑；普通构建无需运行它。JSON 组件版本由 `dependencies.lock` 固定。

`tools/export_ota.py --build-dir build --output-dir release` 可导出带版本号的 OTA 应用及 SHA-256 清单。`tools/verify_ota.py` 是连接设备后的显式硬件测试工具，默认仅查询版本；运行 `--mode negative` 或 `--mode upload` 前请阅读脚本说明。

## 主机侧检查

```sh
node tests/setup.test.cjs
node tests/ota.test.cjs
python tests/test_release.py
```

检查涵盖省市数据、设置交互、分时亮度、升级文件筛选、上传和设置互斥，以及分区边界。真实跨午夜、长期负载和全部天气码未穷举验证。
