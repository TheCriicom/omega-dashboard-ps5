<p align="center">
  <picture><source media="(prefers-color-scheme: dark)" srcset="docs/img/omega-mark.svg"><img src="docs/img/omega-mark-light.svg" width="96" alt="Omega"></picture>
</p>

<h1 align="center">Omega</h1>

<p align="center">
  專為已啟用自製軟體的 PS5 主機打造的開源儀表板：遊戲、Store、音樂、好友與派對，全都集中在同一處。<br>
  由 <a href="https://outlinedigital.it"><b>TheCriicom</b></a> 開發。
</p>

<p align="center"><sub><a href="README.md">English</a> · <a href="README.it.md">Italiano</a> · <a href="README.ja.md">日本語</a> · <a href="README.fr.md">Français</a> · <a href="README.es.md">Español</a> · <a href="README.de.md">Deutsch</a> · <a href="README.nl.md">Nederlands</a> · <a href="README.pt-PT.md">Português (Portugal)</a> · <a href="README.pt-BR.md">Português (Brasil)</a> · <a href="README.ru.md">Русский</a> · <a href="README.ko.md">한국어</a> · <a href="README.zh-Hans.md">简体中文</a> · <b>繁體中文</b> · <a href="README.fi.md">Suomi</a> · <a href="README.sv.md">Svenska</a> · <a href="README.da.md">Dansk</a> · <a href="README.nb.md">Norsk bokmål</a> · <a href="README.pl.md">Polski</a> · <a href="README.tr.md">Türkçe</a> · <a href="README.cs.md">Čeština</a> · <a href="README.hu.md">Magyar</a> · <a href="README.el.md">Ελληνικά</a> · <a href="README.ro.md">Română</a> · <a href="README.th.md">ไทย</a> · <a href="README.vi.md">Tiếng Việt</a> · <a href="README.id.md">Bahasa Indonesia</a> · <a href="README.uk.md">Українська</a></sub></p>

<p align="center">
  <a href="https://play.omegasuite.it">網站</a> ·
  <a href="https://play.omegasuite.it/installa">安裝</a> ·
  <a href="server/README.md">架設伺服器</a> ·
  <a href="client/README.md">開發</a>
</p>

![Omega Home](docs/screenshots/home.png)

Omega 是專為自製軟體使用者設計的儀表板，只要你願意，它也能成為你的 Home
（首次啟動時會詢問）：已安裝的遊戲與自製軟體並列在同一排，Store 只要按一個
按鈕就能安裝，玩遊戲時音樂不會中斷，好友也永遠近在一鍵之間。它以自製軟體的
形式執行（SDL2、軟體算繪器），並連線到任何人都能架設的伺服器。

## 功能一覽

- **Home**：遊戲與自製軟體並列，背景會依據美術圖動態變化，
  可直接啟動（遊戲透過 LncUtil、自製軟體透過 websrv、ELF
  payload 在背景執行）。
- 開源自製軟體的 **Store**：搜尋、分類貨架、投票、評分
  與留言。它會自動辨識 `.pkg`、`.zip` 和 `.elf`，並將
  每個檔案安裝到正確的位置。
- **我的遊戲庫**：你自己擁有的遊戲備份，附有下載連結與封面，可從主機或手機
  新增，也能從 JSON 檔案匯入（匯入一次，或建立連結以保持同步）。只儲存在主機上。
- **音樂**，遊戲中也能聽：網路電台（radio-browser）、Navidrome 與其他 Subsonic
  伺服器、USB 檔案、任何音訊連結。播放由背景常駐程式負責（FFmpeg 純音訊版本），
  所以啟動遊戲時不會中斷。
- **手機遙控**：常駐程式會在 9095 連接埠提供一個網頁。掃描主機上顯示的 QR 碼，
  輸入 PIN，就能用任何手機、平板或電腦的瀏覽器控制音樂、傳送音訊檔，並管理
  我的遊戲庫。
- **HEN 偵測**：啟動時 Omega 會辨識 OnionHEN、etaHEN、pldmgr 或 ps5_autoloader，
  告訴你缺少什麼，並在你確認後，將相關服務安裝並啟用到正確的位置。在 OnionHEN
  上，它還會在遊戲內選單（L2 + R3）新增一個附音樂控制的頁面。
- **系統工具**：溫度、風扇閾值、儲存空間與檔案管理員。
- **Community**：可發文、按讚與留言的動態牆；群組聊天；
  你可能認識的人；遊戲時間與好友排行榜。
- **Party**：支援文字與語音聊天（Opus）、遊戲邀請、自訂狀態（線上、
  離開、請勿打擾、隱身）。
- **Game Base**：好友、好友邀請與訊息。
- **隱私**：封鎖、檢舉、設定誰能傳訊息給你、匯出資料、
  刪除帳號。
- **主題**、自動生成的環境音樂、閱讀用瀏覽器。
- **27 種語言**：App 會自動採用主機的語言（也可以
  在「設定」中更改）。
- **自選伺服器**：在「設定」中加入任何 Omega 伺服器的位址；
  官方伺服器永遠可用。

| | |
|---|---|
| ![Store](docs/screenshots/store.png) | ![Community](docs/screenshots/community.png) |
| ![Party](docs/screenshots/party.png) | ![遊戲時間](docs/screenshots/stats.png) |
| ![電腦上的手機遙控](docs/screenshots/remote-music.png) | ![電腦上的我的遊戲庫](docs/screenshots/remote-library.png) |

## 儲存庫結構

| 資料夾 | |
|---|---|
| [`client/`](client) | 主機 App（C、SDL2），以及可在 Mac 上開發用的桌面版建置 |
| [`daemon/`](daemon) | 背景 payload：音樂播放器、手機遙控網頁、我的遊戲庫、遊戲中的通知，以及（僅在你選擇時）返回 Home 時重新開啟 Omega |
| [`onionhen-plugin/`](onionhen-plugin) | OnionHEN 外掛：在遊戲內選單新增附音樂控制的 Omega 頁面 |
| [`server/`](server) | Node.js API、代理伺服器、管理面板，以及用於架設伺服器的 Docker Compose |
| [`docs/`](docs) | 架構說明與圖片 |

## 在主機上安裝 Omega

你需要一台已越獄且支援自製軟體的 PS5：OnionHEN 或 etaHEN 這類 HEN，或是
[websrv](https://github.com/ps5-payload-dev/websrv) 這類啟動器，再搭配 payload 載入器。
最簡單的方式是用主機的瀏覽器開啟
[play.omegasuite.it/installa](https://play.omegasuite.it/installa)。
或者，也可以從網站下載安裝包，再透過 FTP 將 `data/` 複製到主機上。

## 架設伺服器

```sh
git clone https://github.com/CristianLaporta/omega-dashboard-ps5.git
cd omega-dashboard-ps5/server
./scripts/install.sh
```

此指令碼會建立含有隨機密鑰的 `.env` 並啟動容器。
包含透過 Caddy 自動設定 HTTPS 的完整指南，請參閱
[`server/README.md`](server/README.md)。接著在主機上前往：
**設定 → 伺服器 → 新增伺服器**。

## 開發

- App：[`client/README.md`](client/README.md)——使用
  [ps5-payload-sdk](https://github.com/ps5-payload-dev/sdk) 進行 PS5 建置，以及使用
  Homebrew 的 SDL2 進行桌面版建置；桌面版可由指令檔驅動，無需主機即可測試介面。
  翻譯檔位於 `client/i18n/`（每種語言一個 JSON）。
- 伺服器：[`server/README.md`](server/README.md)——Node.js ≥ 20 與 PostgreSQL，
  以 `npm test` 執行端對端測試。
- 架構：[`docs/architecture.md`](docs/architecture.md)。

歡迎參與貢獻：請參閱 [CONTRIBUTING.md](CONTRIBUTING.md)。

## 作者

Omega 由 **TheCriicom** 開發——[outlinedigital.it](https://outlinedigital.it)。

## 授權條款

Copyright © 2026 TheCriicom 與 Omega 貢獻者。
Omega 是自由軟體：[GNU GPL v3 或更新版本](LICENSE)。第三方元件
及其授權條款列於
[`client/THIRD-PARTY-NOTICES.md`](client/THIRD-PARTY-NOTICES.md)。

Omega 是獨立專案，與 Sony Interactive Entertainment 並無任何關聯，
亦未獲其認可或贊助。「PlayStation」與「PS5」為其各自所有者的
商標。Omega 不包含也不散布任何遊戲。
