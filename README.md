# SimplePlayer

一个 Windows x64 的原生播放器。最初是为验证仅依靠 LAV filter 是否可以实现一个播放器。

**本项目是为了验证自己的几个想法，以及后续做研究使用**

**初版 v0.1.0**。主体源码采用 GPL-3.0-only，第三方组件保留原许可证。

## 介绍

可播放本地媒体与Bilibili视频，由 LAV / MPC Video Renderer 播放，Bilibili 链接由用户安装的 AngelScript 脚本解析。

## 运行

使用 Windows 构建包时，解压并保留整个目录，运行 `SimplePlayer.exe`。

| 运行依赖 | 用途 |
|---|---|
| 用户安装的 x64 [LAV Filters](https://github.com/Nevcairiel/LAVFilters/releases) | 媒体读取和解码 |
| 用户安装的 x64 [MPC Video Renderer](https://github.com/Aleksoid1978/VideoRenderer/releases) | 视频与字幕输出 |
| [WebView2 Runtime](https://developer.microsoft.com/en-us/microsoft-edge/webview2/) | 登录与聊天 |
| 用户提供的 [BilibiliPotPlayer](https://github.com/frostnotfall/BilibiliPotPlayer) 脚本 | Bilibili 解析 |
| 用户提供的 FFmpeg 程序 | 直播缓存回看 |

账号、设置和播放历史默认保存在程序旁的 `data/`；可用 `--data-dir <目录>` 指定位置。需要直连时使用 `--direct-network`，只影响播放器进程。

| 操作 | 快捷方式 |
|---|---|
| 暂停 / 继续 | 空格、双击视频 |
| 全屏 / 取消全屏 | 鼠标中键、F11；Esc 退出 |
| 跳转 | 左右方向键、进度条 |
| 调整音量 | 音量控件上滚轮 |
| 缩放播放列表条目 | Ctrl + 滚轮 |
| 输入链接 | Ctrl + L |
| 打开 / 关闭播放列表 | F6 |
| 打开 / 关闭聊天 | F9（启用 AngelScript 时） |
| 加快 / 降低播放速度 | C / X，每次 0.25 倍 |
| 恢复正常速度 | Z |

## 从源码构建

环境：Windows x64、PowerShell 7、Python 3.10+、Git、Visual Studio C++ 桌面工具及 Windows SDK。当前构建使用 MSVC 工具集 14.44、Qt 6.8.3 / msvc2022_64。

```powershell
git clone https://github.com/frostnotfall/SimplePlayer.git
cd SimplePlayer
./tools/bootstrap.ps1
./tools/install-qt.ps1
./tools/install-subtitles.ps1
./tools/install-audio.ps1
./tools/build.ps1 -Package
```

产物位于 `dist/SimplePlayer/`。依赖下载到 `.deps/`，构建工具放在 `.tools/`；二者不提交。可选 `./tools/install-live-cache.ps1 -Ffmpeg '<FFmpeg.exe 路径>'` 将自己的 FFmpeg 导入开发包。构建不会自动分发 Bilibili 解析脚本或 BFRC。

构建运行 CTest。播放集成验证使用合成媒体和隔离数据目录：

```powershell
./tools/integration-test.ps1 -Ffmpeg '<FFmpeg.exe 路径>'
./tools/live-playlist-test.ps1 -Ffmpeg '<FFmpeg.exe 路径>'
./tools/final-features-test.ps1 -Ffmpeg '<FFmpeg.exe 路径>'
./tools/playback-controls-test.ps1 -Ffmpeg '<FFmpeg.exe 路径>'
./tools/subtitle-mode-test.ps1
./tools/subtitle-scroll-test.ps1
./tools/window-layout-test.ps1
./tools/diagnostic-test.ps1
```

## 当前限制

初版已在开发机器上验证普通播放、双字幕、切流、全屏、播放列表及合成直播回看。干净 Windows 环境、更多 GPU/DPI 和长期真实直播仍待验证。

当前 stock LAV 的逐 URL Cookie、分片策略和阻塞打开取消存在限制；登录状态不等于所有受限媒体都能播放。

## 发布与许可证

源码和 Windows 下载包分别管理，编译产物作为 GitHub Release 附件。仓库不包含账号数据、浏览器 Profile、媒体缓存或本机编译目录。

- [GPL v3 许可证](LICENSE)
- [第三方说明](packaging/THIRD_PARTY_NOTICES.md)
- [依赖版本清单](packaging/source-manifest.json)

报告问题时请提供版本、Windows/GPU/滤镜版本、复现步骤及脱敏诊断；不要公开 Cookie、账号配置或含签名参数的媒体 URL。
