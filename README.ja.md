<p align="center">
  <picture><source media="(prefers-color-scheme: dark)" srcset="docs/img/omega-mark.svg"><img src="docs/img/omega-mark-light.svg" width="96" alt="Omega"></picture>
</p>

<h1 align="center">Omega</h1>

<p align="center">
  homebrew 対応 PS5 のためのオープンソースのダッシュボード。ゲーム、Store、音楽、フレンド、パーティーをひとつの場所に。<br>
  開発：<a href="https://outlinedigital.it"><b>TheCriicom</b></a>
</p>

<p align="center"><sub><a href="README.md">English</a> · <a href="README.it.md">Italiano</a> · <b>日本語</b> · <a href="README.fr.md">Français</a> · <a href="README.es.md">Español</a> · <a href="README.de.md">Deutsch</a> · <a href="README.nl.md">Nederlands</a> · <a href="README.pt-PT.md">Português (Portugal)</a> · <a href="README.pt-BR.md">Português (Brasil)</a> · <a href="README.ru.md">Русский</a> · <a href="README.ko.md">한국어</a> · <a href="README.zh-Hans.md">简体中文</a> · <a href="README.zh-Hant.md">繁體中文</a> · <a href="README.fi.md">Suomi</a> · <a href="README.sv.md">Svenska</a> · <a href="README.da.md">Dansk</a> · <a href="README.nb.md">Norsk bokmål</a> · <a href="README.pl.md">Polski</a> · <a href="README.tr.md">Türkçe</a> · <a href="README.cs.md">Čeština</a> · <a href="README.hu.md">Magyar</a> · <a href="README.el.md">Ελληνικά</a> · <a href="README.ro.md">Română</a> · <a href="README.th.md">ไทย</a> · <a href="README.vi.md">Tiếng Việt</a> · <a href="README.id.md">Bahasa Indonesia</a> · <a href="README.uk.md">Українська</a></sub></p>

<p align="center">
  <a href="https://play.omegasuite.it">ウェブサイト</a> ·
  <a href="https://play.omegasuite.it/installa">インストール</a> ·
  <a href="server/README.md">サーバーを運用する</a> ·
  <a href="client/README.md">開発</a>
</p>

![Omega の Home](docs/screenshots/home.png)

Omega は homebrew ユーザーのために作られたダッシュボードで、お好みで Home 画面としても使えます
（初回起動時に確認されます）。インストール済みのゲームと homebrew が同じ列に並び、
Store はボタンひとつでインストールでき、ゲーム中も音楽が流れ続け、
フレンドにはいつでもワンボタンでアクセスできます。homebrew として動作し
（SDL2、ソフトウェアレンダラー）、誰でも運用できるサーバーと通信します。

## 主な機能

- **Home**：ゲームと homebrew を一緒に表示。アートワークから生成される動的な背景、
  ダイレクト起動（ゲームは LncUtil、homebrew は websrv 経由、ELF ペイロードは
  バックグラウンドで実行）。
- **Store**：オープンソースの homebrew を集めたストア。検索、カテゴリー別の棚、投票、
  評価、コメント。`.pkg`、`.zip`、`.elf` を自動で判別し、それぞれ適切な場所に
  インストールします。
- **マイライブラリ**：所有しているゲームのバックアップを、ダウンロードリンクとカバー画像つきで管理。
  本体またはスマートフォンから追加でき、JSON ファイルからのインポート
  （一度だけ、または同期を保つようリンク）にも対応。データは本体にのみ保存されます。
- **音楽**（ゲーム中も再生）：インターネットラジオ（radio-browser）、Navidrome などの
  Subsonic サーバー、USB のファイル、任意のオーディオリンク。再生はバックグラウンドの
  デーモン（FFmpeg の音声専用ビルド）で行われるため、ゲームを起動しても止まりません。
- **スマートフォンリモコン**：デーモンがポート 9095 で Web ページを提供します。
  本体に表示される QR コードを読み取り、PIN を入力すると、スマートフォン、タブレット、
  PC のブラウザーから音楽の操作、音声ファイルの送信、マイライブラリの管理ができます。
- **HEN の検出**：起動時に Omega が OnionHEN、etaHEN、pldmgr、ps5_autoloader を認識し、
  足りないものを知らせ、確認後にそのサービスを適切な場所へインストールして有効にします。
  OnionHEN ではゲーム中メニュー（L2 + R3）に、音楽コントロール付きのページを追加します。
- **システムツール**：温度、ファンのしきい値、ストレージ、ファイルマネージャー。
- **Community**：投稿・いいね・コメントができるウォール、グループチャット、
  知り合いかもしれない人、プレイ時間とフレンドランキング。
- **Party**：チャットとボイス（Opus）、ゲームへの招待、カスタムステータス
  （オンライン、退席中、取り込み中、オフライン表示）。
- **Game Base**：フレンド、フレンドリクエスト、メッセージ。
- **プライバシー**：ブロック、通報、メッセージを送れる相手の設定、データの
  エクスポート、アカウントの削除。
- **テーマ**、自動生成のアンビエント音楽、読書向けブラウザー。
- **27 言語**：アプリは本体の言語に自動で合わせます（設定から変更も可能）。
- **サーバーを選べる**：設定から任意の Omega サーバーのアドレスを追加できます。
  公式サーバーは常に利用可能です。

| | |
|---|---|
| ![Store](docs/screenshots/store.png) | ![Community](docs/screenshots/community.png) |
| ![Party](docs/screenshots/party.png) | ![プレイ時間](docs/screenshots/stats.png) |
| ![PC でのスマートフォンリモコン](docs/screenshots/remote-music.png) | ![PC からのマイライブラリ](docs/screenshots/remote-library.png) |

## リポジトリ構成

| フォルダー | |
|---|---|
| [`client/`](client) | 本体用アプリ（C、SDL2）と、Mac で開発するためのデスクトップビルド |
| [`daemon/`](daemon) | バックグラウンドのペイロード：音楽プレーヤー、スマートフォンリモコンの Web ページ、マイライブラリ、ゲーム中の通知、そして（選んだ場合のみ）Home に戻ったときの Omega の再起動 |
| [`onionhen-plugin/`](onionhen-plugin) | OnionHEN プラグイン：ゲーム中メニューに音楽コントロール付きの Omega ページを追加します |
| [`server/`](server) | Node.js の API、プロキシ、モデレーションパネル、サーバー運用用の Docker Compose |
| [`docs/`](docs) | アーキテクチャと画像 |

## 本体への Omega のインストール

homebrew に対応したジェイルブレイク済みの PS5 が必要です。OnionHEN や etaHEN などの HEN、または
[websrv](https://github.com/ps5-payload-dev/websrv) などのランチャーとペイロードローダーを使います。
いちばん簡単なのは、本体のブラウザーで
[play.omegasuite.it/installa](https://play.omegasuite.it/installa) を開く方法です。
または、ウェブサイトからパッケージをダウンロードし、`data/` を FTP で本体にコピーします。

## サーバーの運用

```sh
git clone https://github.com/CristianLaporta/omega-dashboard-ps5.git
cd omega-dashboard-ps5/server
./scripts/install.sh
```

このスクリプトはランダムなシークレットを含む `.env` を作成し、コンテナを起動します。
Caddy による自動 HTTPS を含む詳しいガイドは
[`server/README.md`](server/README.md) にあります。その後、本体で
**設定 → サーバー → サーバーを追加** を選びます。

## 開発

- アプリ：[`client/README.md`](client/README.md) —
  [ps5-payload-sdk](https://github.com/ps5-payload-dev/sdk) による PS5 ビルドと、
  Homebrew の SDL2 を使うデスクトップビルド。コマンドファイルで操作でき、本体なしで UI をテストできます。
  翻訳は `client/i18n/` にあります（言語ごとに JSON ひとつ）。
- サーバー：[`server/README.md`](server/README.md) — Node.js ≥ 20 と PostgreSQL、
  `npm test` によるエンドツーエンドテスト。
- アーキテクチャ：[`docs/architecture.md`](docs/architecture.md)。

コントリビューションを歓迎します。[CONTRIBUTING.md](CONTRIBUTING.md) をご覧ください。

## 作者

Omega は **TheCriicom** が開発しています — [outlinedigital.it](https://outlinedigital.it)。

## ライセンス

Copyright © 2026 TheCriicom and the Omega contributors.
Omega はフリーソフトウェアです：[GNU GPL v3 以降](LICENSE)。サードパーティー製
コンポーネントとそのライセンスは
[`client/THIRD-PARTY-NOTICES.md`](client/THIRD-PARTY-NOTICES.md) に記載されています。

Omega は独立したプロジェクトであり、Sony Interactive Entertainment との提携関係はなく、
同社による承認や後援も受けていません。「PlayStation」および「PS5」は、それぞれの
所有者の商標です。Omega はゲームを含まず、配布もしません。
