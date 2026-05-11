# Shared KM — 跨平台键鼠共享

一套键盘鼠标控制多台 Windows 电脑，面向局域网低延迟输入共享。

## 特性

- **统一 EXE**：一个程序同时运行发送端和接收端，图形界面管理
- **两种模式**：
  - **拷贝模式**：发送端鼠标移动到哪，接收端就跟到哪
  - **扩展模式**：鼠标推到屏幕边缘"跨越"到接收端，回来后恢复正常
- **UDP 传输**：低延迟，无 TCP 重传阻塞
- **鼠标移动合并**：8ms 合并一次最新位置，不丢帧但减少网络包
- **跨屏阻断**：跨越到接收端后，发送端本地鼠标点击/滚轮被拦截，不会误操作
- **剪切板同步**：发送端复制文本自动同步到接收端
- **暂停开关**：Scroll Lock 键随时暂停/恢复发送
- **系统托盘**：最小化到托盘后台运行
- **网络发现**：自动扫描局域网内的接收端

## 快速开始

### 下载

从接收端电脑的 HTTP 共享下载：`http://<发送端IP>:8888/shared_km.exe`

或用浏览器访问目录列表选择版本文件下载。

### 运行

1. 在接收端电脑上打开程序，点击 **○ Receiver** 启动接收端
2. 在发送端电脑上打开程序，填入接收端 IP，点击 **○ Sender** 启动发送端
3. 或者直接用 **Discover** 按钮自动发现局域网内的接收端

### 模式选择

- **Copy (Mirror)**：发送端鼠标完全镜像到接收端
- **Extend (Edge)**：鼠标推到屏幕右边缘触发跨越，按 Esc 或向左拖拽返回

### 快捷键

| 按键 | 功能 |
|------|------|
| Scroll Lock | 暂停/恢复发送端 |
| Esc | 扩展模式下退出跨屏状态 |

## 构建

详见 [BUILDING.md](BUILDING.md)。

环境要求：Visual Studio 2022 Build Tools + CMake。

```bat
cmake -S . -B build-fresh
cmake --build build-fresh --config Release --target shared_km
```

## 架构

```
发送端 (Sender)                         接收端 (Receiver)
    │                                       │
    ├─ WH_MOUSE_LL / WH_KEYBOARD_LL         ├─ TCP/UDP 接收
    ├─ RawInputReader (WM_INPUT)            ├─ InputInjector (SendInput)
    ├─ ClipboardMonitor                     ├─ 剪切板写入
    └─ UDP 发送 ──────────────────────►     └─ 鼠标定位 (EdgeEnter)
```

- 二进制协议：`MessageHeader` (magic=SHKM, version, kind, payload_size) + payload
- 传输层：UDP，每条消息一个数据报
- 注入标记：`kInjectedMouseMarker = 0x53484B4D4F555345` 防止回环
