# OMG Network Monitor

Windows 桌面网络监控器，采用 **Rust + Tauri 2 + 原生 Web UI** 重构。Rust 后端负责系统采集、日志、配置与 Windows 操作；前端只负责呈现和交互，不使用 Material UI。

## 功能

- 监控全系统 IPv4/IPv6 TCP 与 UDP 端点，显示进程、PID、可读取的可执行文件路径、协议、本地/远端地址及 TCP 状态。
- 记录连接的打开、关闭、首次发现和最后发现时间；已关闭连接按配置暂存。
- 汇总活动网卡的累计上传/下载和实时速率，并显示最近两分钟的速度曲线。
- 标记常见远控端口，或进程路径无法读取的对外 TCP 连接，作为“建议复核”。
- 支持搜索、暂停/恢复、复制连接详情、CSV 快照导出、JSONL 事件/分钟指标日志和日志自动清理。
- 支持编辑配置、当前用户开机启动、关闭窗口后继续在后台运行。
- 支持为选中连接创建 Windows 出站防火墙规则；恢复时会一次清理该程序由本工具创建的全部规则。被阻断的项目会保留在列表中，并提供“恢复全部联网”处理历史残留规则。防火墙操作需要以管理员身份运行应用。

## 运行与构建

需要 Node.js、npm 和 Rust stable 工具链：

```powershell
npm install
npm run tauri dev
```

构建 Windows 安装包：

```powershell
& .\build.ps1
```

产物会由 Tauri 生成到 `src-tauri\target\release\bundle`。

## 配置与日志

首次运行会在以下目录创建配置文件和日志：

```text
%LOCALAPPDATA%\OMG Network Monitor\network-monitor.yaml
%LOCALAPPDATA%\OMG Network Monitor\logs
```

配置修改后，在应用中点击“重载配置”即可生效。

## 权限和边界

普通用户可以读取大多数系统连接；部分受保护进程的路径可能不可访问。系统网络统计只提供整机网卡流量，不提供准确的逐进程字节数。HTTPS/TLS/QUIC 内容不会被解密或检查。
