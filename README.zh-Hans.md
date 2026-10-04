<p align="center">
  <picture><source media="(prefers-color-scheme: dark)" srcset="docs/img/omega-mark.svg"><img src="docs/img/omega-mark-light.svg" width="96" alt="Omega"></picture>
</p>

<h1 align="center">Omega</h1>

<p align="center">
  面向已开启自制程序的 PS5 的开源仪表盘：游戏、Store、音乐、好友和 Party，尽在一处。<br>
  由 <a href="https://outlinedigital.it"><b>TheCriicom</b></a> 开发。
</p>

<p align="center"><sub><a href="README.md">English</a> · <a href="README.it.md">Italiano</a> · <a href="README.ja.md">日本語</a> · <a href="README.fr.md">Français</a> · <a href="README.es.md">Español</a> · <a href="README.de.md">Deutsch</a> · <a href="README.nl.md">Nederlands</a> · <a href="README.pt-PT.md">Português (Portugal)</a> · <a href="README.pt-BR.md">Português (Brasil)</a> · <a href="README.ru.md">Русский</a> · <a href="README.ko.md">한국어</a> · <b>简体中文</b> · <a href="README.zh-Hant.md">繁體中文</a> · <a href="README.fi.md">Suomi</a> · <a href="README.sv.md">Svenska</a> · <a href="README.da.md">Dansk</a> · <a href="README.nb.md">Norsk bokmål</a> · <a href="README.pl.md">Polski</a> · <a href="README.tr.md">Türkçe</a> · <a href="README.cs.md">Čeština</a> · <a href="README.hu.md">Magyar</a> · <a href="README.el.md">Ελληνικά</a> · <a href="README.ro.md">Română</a> · <a href="README.th.md">ไทย</a> · <a href="README.vi.md">Tiếng Việt</a> · <a href="README.id.md">Bahasa Indonesia</a> · <a href="README.uk.md">Українська</a></sub></p>

<p align="center">
  <a href="https://play.omegasuite.it">网站</a> ·
  <a href="https://play.omegasuite.it/installa">安装</a> ·
  <a href="server/README.md">自建服务器</a> ·
  <a href="client/README.md">开发</a>
</p>

![Omega 的 Home](docs/screenshots/home.png)

Omega 是一个专为自制程序用户打造的仪表盘，只要你愿意，它也可以成为你的 Home
（首次启动时会询问）：已安装的游戏和自制程序排在同一行，Store 一键安装，玩游戏
时音乐不会中断，好友随时一按即达。它以自制程序的形式运行（SDL2，软件渲染），
并与任何人都可以自行搭建的服务器通信。

## 功能一览

- **Home**：游戏与自制程序放在一起，根据封面图生成动态背景，直接启动（游戏通过
  LncUtil，自制程序通过 websrv，ELF payload 在后台运行）。
- **Store**：开源自制程序商店，支持搜索、分类货架、投票、评分和评论。能自动识别
  `.pkg`、`.zip` 和 `.elf`，并将每个文件安装到正确的位置。
- **我的游戏库**：你自己拥有的游戏备份，附带下载链接和封面，可在主机或手机上
  添加，也可从 JSON 文件导入（一次性导入，或建立关联以保持同步）。仅保存在主机上。
- **音乐**，游戏中也能听：网络电台（radio-browser）、Navidrome 和其他 Subsonic
  服务器、USB 文件、任意音频链接。播放由后台守护进程负责（FFmpeg 纯音频构建），
  所以启动游戏时不会中断。
- **手机遥控**：守护进程会在 9095 端口提供一个网页。扫描主机上显示的 QR 码，输入
  PIN，就能在任意手机、平板或电脑浏览器上控制音乐、发送音频文件并管理我的游戏库。
- **HEN 检测**：启动时 Omega 会识别 OnionHEN、etaHEN、pldmgr 或 ps5_autoloader，
  告诉你缺少什么，并在你确认后把相关服务安装并启用到正确的位置。在 OnionHEN 上，
  它还会在游戏内菜单（L2 + R3）中添加一个带音乐控制的页面。
- **系统工具**：温度、风扇阈值、存储空间和文件管理器。
- **Community**：包含帖子、点赞和评论的动态墙；群聊；你可能认识的人；游戏时长和
  好友排行榜。
- **Party**：文字聊天和语音（Opus），游戏邀请，自定义状态（在线、离开、请勿打扰、
  隐身）。
- **Game Base**：好友、好友请求和消息。
- **隐私**：屏蔽、举报、设置谁可以给你发消息、导出数据、删除账号。
- **主题**、生成式环境音乐、阅读浏览器。
- **27 种语言**：应用会自动跟随主机语言（也可以在设置中更改）。
- **自选服务器**：在设置中添加任意 Omega 服务器的地址；官方服务器始终可用。

| | |
|---|---|
| ![Store](docs/screenshots/store.png) | ![Community](docs/screenshots/community.png) |
| ![Party](docs/screenshots/party.png) | ![游戏时长](docs/screenshots/stats.png) |
| ![电脑上的手机遥控](docs/screenshots/remote-music.png) | ![电脑上的我的游戏库](docs/screenshots/remote-library.png) |

## 仓库结构

| 文件夹 | |
|---|---|
| [`client/`](client) | 主机端应用（C、SDL2），以及用于在 Mac 上开发的桌面版构建 |
| [`daemon/`](daemon) | 后台 payload：音乐播放器、手机遥控网页、我的游戏库、游戏中的通知，以及（仅在你选择时）返回 Home 时重新打开 Omega |
| [`onionhen-plugin/`](onionhen-plugin) | OnionHEN 插件：在游戏内菜单中添加带音乐控制的 Omega 页面 |
| [`server/`](server) | Node.js API、代理、审核面板，以及用于搭建服务器的 Docker Compose |
| [`docs/`](docs) | 架构文档和图片 |

## 在主机上安装 Omega

你需要一台已越狱并支持自制程序的 PS5：OnionHEN 或 etaHEN 这样的 HEN，或者
[websrv](https://github.com/ps5-payload-dev/websrv) 这样的启动器，加上一个
payload 加载器。最简单的方式是用主机的浏览器打开
[play.omegasuite.it/installa](https://play.omegasuite.it/installa)。也可以从网站
下载安装包，再通过 FTP 将 `data/` 复制到主机上。

## 自建服务器

```sh
git clone https://github.com/TheCriicom/omega-dashboard-ps5.git
cd omega-dashboard-ps5/server
./scripts/install.sh
```

该脚本会创建带有随机密钥的 `.env` 并启动容器。包含通过 Caddy 自动配置 HTTPS
的完整指南见 [`server/README.md`](server/README.md)。然后在主机上进入：
**设置 → 服务器 → 添加服务器**。

## 开发

- 应用：[`client/README.md`](client/README.md) —— 使用
  [ps5-payload-sdk](https://github.com/ps5-payload-dev/sdk) 构建 PS5 版本，并使用
  Homebrew 的 SDL2 构建桌面版；桌面版可通过命令文件驱动，无需主机即可测试界面。
  翻译文件位于 `client/i18n/`（每种语言一个 JSON）。
- 服务器：[`server/README.md`](server/README.md) —— Node.js ≥ 20 和 PostgreSQL，
  使用 `npm test` 运行端到端测试。
- 架构：[`docs/architecture.md`](docs/architecture.md)。

欢迎贡献：请参阅 [CONTRIBUTING.md](CONTRIBUTING.md)。

## 作者

Omega 由 **TheCriicom** 开发 —— [outlinedigital.it](https://outlinedigital.it)。

## 许可证

Copyright © 2026 TheCriicom 及 Omega 贡献者。
Omega 是自由软件：[GNU GPL v3 或更高版本](LICENSE)。第三方组件及其许可证列于
[`client/THIRD-PARTY-NOTICES.md`](client/THIRD-PARTY-NOTICES.md)。

Omega 是一个独立项目，与 Sony Interactive Entertainment 没有任何关联，也未获得
其认可或赞助。“PlayStation”和“PS5”是其各自所有者的商标。Omega 不包含也不分发
任何游戏。
