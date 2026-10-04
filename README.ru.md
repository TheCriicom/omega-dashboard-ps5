<p align="center">
  <picture><source media="(prefers-color-scheme: dark)" srcset="docs/img/omega-mark.svg"><img src="docs/img/omega-mark-light.svg" width="96" alt="Omega"></picture>
</p>

<h1 align="center">Omega</h1>

<p align="center">
  Дашборд с открытым исходным кодом для PS5 с поддержкой homebrew: игры, Store, музыка, друзья и Party в одном месте.<br>
  Разработчик — <a href="https://outlinedigital.it"><b>TheCriicom</b></a>.
</p>

<p align="center"><sub><a href="README.md">English</a> · <a href="README.it.md">Italiano</a> · <a href="README.ja.md">日本語</a> · <a href="README.fr.md">Français</a> · <a href="README.es.md">Español</a> · <a href="README.de.md">Deutsch</a> · <a href="README.nl.md">Nederlands</a> · <a href="README.pt-PT.md">Português (Portugal)</a> · <a href="README.pt-BR.md">Português (Brasil)</a> · <b>Русский</b> · <a href="README.ko.md">한국어</a> · <a href="README.zh-Hans.md">简体中文</a> · <a href="README.zh-Hant.md">繁體中文</a> · <a href="README.fi.md">Suomi</a> · <a href="README.sv.md">Svenska</a> · <a href="README.da.md">Dansk</a> · <a href="README.nb.md">Norsk bokmål</a> · <a href="README.pl.md">Polski</a> · <a href="README.tr.md">Türkçe</a> · <a href="README.cs.md">Čeština</a> · <a href="README.hu.md">Magyar</a> · <a href="README.el.md">Ελληνικά</a> · <a href="README.ro.md">Română</a> · <a href="README.th.md">ไทย</a> · <a href="README.vi.md">Tiếng Việt</a> · <a href="README.id.md">Bahasa Indonesia</a> · <a href="README.uk.md">Українська</a></sub></p>

<p align="center">
  <a href="https://play.omegasuite.it">Сайт</a> ·
  <a href="https://play.omegasuite.it/installa">Установка</a> ·
  <a href="server/README.md">Свой сервер</a> ·
  <a href="client/README.md">Разработка</a>
</p>

![Home в Omega](docs/screenshots/home.png)

Omega — это дашборд, созданный для пользователей homebrew, и ваш экран Home,
если вы этого захотите (он спросит при первом запуске): установленные игры и
homebrew-приложения стоят в одном ряду, Store устанавливает всё одной кнопкой,
музыка продолжает играть во время игры, а друзья всегда в одном нажатии. Omega
работает как homebrew (SDL2, программный рендеринг) и подключается к серверу,
который может развернуть кто угодно.

## Возможности

- **Home**, где игры и homebrew собраны вместе, с динамическими фонами из
  обложек и прямым запуском (игры через LncUtil, homebrew через websrv,
  ELF-пейлоады в фоне).
- **Store** с homebrew-приложениями с открытым кодом: поиск, полки по
  категориям, голоса, оценки и комментарии. Сам распознаёт `.pkg`, `.zip` и
  `.elf` и устанавливает каждый файл куда нужно.
- **Моя библиотека**: резервные копии ваших собственных игр со ссылкой для
  загрузки и обложкой; добавляются с консоли или с телефона, либо
  импортируются из JSON-файла (один раз или со связью, чтобы всё оставалось
  синхронизированным). Хранится только на консоли.
- **Музыка**, в том числе во время игры: интернет-радио (radio-browser),
  Navidrome и другие серверы Subsonic, файлы с USB, любая ссылка на аудио.
  Воспроизведение работает в фоновом демоне (сборка FFmpeg только для аудио),
  поэтому оно не прерывается, когда вы запускаете игру.
- **Пульт на телефоне**: демон отдаёт веб-страницу на порту 9095. Отсканируйте
  QR-код на консоли, введите PIN, и с браузера любого телефона, планшета или
  ПК можно управлять музыкой, отправлять аудиофайлы и редактировать Мою
  библиотеку.
- **Распознавание HEN**: при запуске Omega определяет OnionHEN, etaHEN, pldmgr
  или ps5_autoloader, сообщает, чего не хватает, и после вашего подтверждения
  устанавливает и включает их службы в нужном месте. В OnionHEN он добавляет в
  игровое меню (L2 + R3) страницу с управлением музыкой.
- **Системные инструменты**: температуры, порог вентилятора, накопитель и
  файловый менеджер.
- **Community**: лента с постами, лайками и комментариями; групповые чаты;
  люди, которых вы можете знать; игровое время и рейтинг среди друзей.
- **Party** с чатом и голосом (Opus), приглашения в игру, собственный статус
  (в сети, отошёл, не беспокоить, невидимка).
- **Game Base**: друзья, заявки и сообщения.
- **Конфиденциальность**: блокировка, жалобы, выбор, кто может вам писать,
  экспорт данных, удаление аккаунта.
- **Темы**, генерируемая фоновая музыка, браузер для чтения.
- **27 языков**: приложение автоматически использует язык консоли (его можно
  сменить в настройках).
- **Выбор сервера**: в настройках можно добавить адрес любого сервера Omega;
  официальный всегда остаётся доступен.

| | |
|---|---|
| ![Store](docs/screenshots/store.png) | ![Community](docs/screenshots/community.png) |
| ![Party](docs/screenshots/party.png) | ![Игровое время](docs/screenshots/stats.png) |
| ![Пульт на телефоне, открытый на ПК](docs/screenshots/remote-music.png) | ![Моя библиотека на ПК](docs/screenshots/remote-library.png) |

## Структура репозитория

| Папка | |
|---|---|
| [`client/`](client) | приложение для консоли (C, SDL2) и десктопная сборка для разработки на Mac |
| [`daemon/`](daemon) | фоновый пейлоад: музыкальный плеер, веб-страница пульта на телефоне, Моя библиотека, уведомления во время игры и (только если вы выберете) повторное открытие Omega при возврате в Home |
| [`onionhen-plugin/`](onionhen-plugin) | плагин для OnionHEN: страница Omega в игровом меню с управлением музыкой |
| [`server/`](server) | API на Node.js, прокси, панель модерации и Docker Compose для развёртывания сервера |
| [`docs/`](docs) | архитектура и изображения |

## Установка Omega на консоль

Нужна PS5 с джейлбрейком и поддержкой homebrew: HEN, например OnionHEN или
etaHEN, либо лаунчер вроде [websrv](https://github.com/ps5-payload-dev/websrv)
с загрузчиком пейлоадов.
Проще всего открыть [play.omegasuite.it/installa](https://play.omegasuite.it/installa)
в браузере консоли. Либо скачайте пакет с сайта и скопируйте `data/` на консоль
по FTP.

## Свой сервер

```sh
git clone https://github.com/TheCriicom/omega-dashboard-ps5.git
cd omega-dashboard-ps5/server
./scripts/install.sh
```

Скрипт создаёт `.env` со случайными секретами и запускает контейнеры. Полное
руководство, включая автоматический HTTPS через Caddy, находится в
[`server/README.md`](server/README.md). Затем на консоли:
**Настройки → Сервер → Добавить сервер**.

## Разработка

- Приложение: [`client/README.md`](client/README.md) — сборка для PS5 с
  [ps5-payload-sdk](https://github.com/ps5-payload-dev/sdk) и десктопная сборка
  с SDL2 из Homebrew, управляемая файлом команд, чтобы проверять интерфейс без
  консоли. Переводы лежат в `client/i18n/` (по одному JSON на язык).
- Сервер: [`server/README.md`](server/README.md) — Node.js ≥ 20 и PostgreSQL,
  сквозные тесты через `npm test`.
- Архитектура: [`docs/architecture.md`](docs/architecture.md).

Мы рады вкладу в проект: см. [CONTRIBUTING.md](CONTRIBUTING.md).

## Автор

Omega разрабатывает **TheCriicom** — [outlinedigital.it](https://outlinedigital.it).

## Лицензия

Copyright © 2026 TheCriicom и участники проекта Omega.
Omega — свободное программное обеспечение: [GNU GPL v3 или более поздняя](LICENSE).
Сторонние компоненты и их лицензии перечислены в
[`client/THIRD-PARTY-NOTICES.md`](client/THIRD-PARTY-NOTICES.md).

Omega — независимый проект; он не связан с Sony Interactive Entertainment, не
одобрен и не спонсируется ею. «PlayStation» и «PS5» являются товарными знаками
соответствующих владельцев. Omega не содержит и не распространяет игры.
