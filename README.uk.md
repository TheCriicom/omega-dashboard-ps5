<p align="center">
  <picture><source media="(prefers-color-scheme: dark)" srcset="docs/img/omega-mark.svg"><img src="docs/img/omega-mark-light.svg" width="96" alt="Omega"></picture>
</p>

<h1 align="center">Omega</h1>

<p align="center">
  Дашборд з відкритим кодом для консолей PS5 з підтримкою homebrew: ігри, Store, музика, друзі й party в одному місці.<br>
  Розробник — <a href="https://outlinedigital.it"><b>TheCriicom</b></a>.
</p>

<p align="center"><sub><a href="README.md">English</a> · <a href="README.it.md">Italiano</a> · <a href="README.ja.md">日本語</a> · <a href="README.fr.md">Français</a> · <a href="README.es.md">Español</a> · <a href="README.de.md">Deutsch</a> · <a href="README.nl.md">Nederlands</a> · <a href="README.pt-PT.md">Português (Portugal)</a> · <a href="README.pt-BR.md">Português (Brasil)</a> · <a href="README.ru.md">Русский</a> · <a href="README.ko.md">한국어</a> · <a href="README.zh-Hans.md">简体中文</a> · <a href="README.zh-Hant.md">繁體中文</a> · <a href="README.fi.md">Suomi</a> · <a href="README.sv.md">Svenska</a> · <a href="README.da.md">Dansk</a> · <a href="README.nb.md">Norsk bokmål</a> · <a href="README.pl.md">Polski</a> · <a href="README.tr.md">Türkçe</a> · <a href="README.cs.md">Čeština</a> · <a href="README.hu.md">Magyar</a> · <a href="README.el.md">Ελληνικά</a> · <a href="README.ro.md">Română</a> · <a href="README.th.md">ไทย</a> · <a href="README.vi.md">Tiếng Việt</a> · <a href="README.id.md">Bahasa Indonesia</a> · <b>Українська</b></sub></p>

<p align="center">
  <a href="https://play.omegasuite.it">Вебсайт</a> ·
  <a href="https://play.omegasuite.it/installa">Встановлення</a> ·
  <a href="server/README.md">Власний сервер</a> ·
  <a href="client/README.md">Розробка</a>
</p>

![Home в Omega](docs/screenshots/home.png)

Omega — це дашборд, створений для користувачів homebrew, і ваш екран Home, якщо
ви цього забажаєте (він запитає під час першого запуску): встановлені ігри й
homebrew стоять в одному ряду, Store встановлює одним натисканням кнопки, музика
продовжує грати під час гри, а друзі завжди поруч — на відстані одного
натискання. Omega працює як homebrew (SDL2, програмний рендеринг) і взаємодіє із
сервером, який може розгорнути будь-хто.

## Що всередині

- **Home**, де ігри й homebrew зібрані разом, динамічні фони з обкладинок,
  прямий запуск (ігри через LncUtil, homebrew через websrv, ELF-пейлоади у
  фоновому режимі).
- **Store** homebrew з відкритим кодом: пошук, полиці за категоріями, голоси,
  оцінки й коментарі. Сам розпізнає `.pkg`, `.zip` і `.elf` та встановлює
  кожен файл у потрібне місце.
- **Моя бібліотека**: резервні копії ваших власних ігор із посиланням для
  завантаження та обкладинкою; додаються з консолі чи з телефона або
  імпортуються з JSON-файлу (один раз або зі зв'язком, щоб усе лишалося
  синхронізованим). Зберігається лише на консолі.
- **Музика**, зокрема й під час гри: інтернет-радіо (radio-browser), Navidrome та
  інші сервери Subsonic, файли з USB, будь-яке посилання на аудіо. Відтворення
  працює у фоновому демоні (збірка FFmpeg лише для аудіо), тому не припиняється,
  коли ви запускаєте гру.
- **Пульт на телефоні**: демон віддає вебсторінку на порту 9095. Скануйте
  QR-код на консолі, введіть PIN, і з браузера будь-якого телефона, планшета чи
  ПК можна керувати музикою, надсилати аудіофайли та керувати Моєю бібліотекою.
- **Розпізнавання HEN**: під час запуску Omega визначає OnionHEN, etaHEN, pldmgr
  або ps5_autoloader, повідомляє, чого бракує, і після вашого підтвердження
  встановлює та вмикає їхні служби в потрібному місці. В OnionHEN він додає до
  ігрового меню (L2 + R3) сторінку з керуванням музикою.
- **Системні інструменти**: температури, поріг вентилятора, сховище та файловий
  менеджер.
- **Community**: стіна з дописами, вподобаннями й коментарями; групові чати;
  люди, яких ви можете знати; ігровий час і рейтинг друзів.
- **Party** з текстовим і голосовим чатом (Opus), запрошеннями до гри, власним
  статусом (у мережі, відійшов, не турбувати, невидимий).
- **Game Base**: друзі, запити й повідомлення.
- **Конфіденційність**: блокування, скарги, хто може вам писати, експорт
  даних, видалення облікового запису.
- **Теми**, згенерована фонова музика, браузер для читання.
- **27 мов**: застосунок автоматично використовує мову консолі (її можна
  змінити в Налаштуваннях).
- **Вибір сервера**: додайте в Налаштуваннях адресу будь-якого сервера Omega;
  офіційний сервер завжди залишається доступним.

| | |
|---|---|
| ![Store](docs/screenshots/store.png) | ![Community](docs/screenshots/community.png) |
| ![Party](docs/screenshots/party.png) | ![Ігровий час](docs/screenshots/stats.png) |
| ![Пульт на телефоні, відкритий на ПК](docs/screenshots/remote-music.png) | ![Моя бібліотека на ПК](docs/screenshots/remote-library.png) |

## Структура репозиторію

| Тека | |
|---|---|
| [`client/`](client) | застосунок для консолі (C, SDL2) і десктопна збірка для розробки на Mac |
| [`daemon/`](daemon) | фоновий пейлоад: музичний програвач, вебсторінка пульта на телефоні, Моя бібліотека, сповіщення під час гри та (лише якщо ви оберете) повторне відкриття Omega, коли ви повертаєтеся в Home |
| [`onionhen-plugin/`](onionhen-plugin) | плагін для OnionHEN: сторінка Omega в ігровому меню з керуванням музикою |
| [`server/`](server) | API на Node.js, проксі, панель модерації та Docker Compose для розгортання сервера |
| [`docs/`](docs) | архітектура й зображення |

## Встановлення Omega на консоль

Потрібна PS5 із джейлбрейком і підтримкою homebrew: HEN, як-от OnionHEN чи
etaHEN, або лаунчер на кшталт [websrv](https://github.com/ps5-payload-dev/websrv)
із завантажувачем пейлоадів.
Найпростіше — відкрити [play.omegasuite.it/installa](https://play.omegasuite.it/installa)
у браузері консолі. Інший варіант — завантажити пакет із сайту й скопіювати
`data/` на консоль через FTP.

## Розгортання сервера

```sh
git clone https://github.com/TheCriicom/omega-dashboard-ps5.git
cd omega-dashboard-ps5/server
./scripts/install.sh
```

Скрипт створює `.env` із випадковими секретами й запускає контейнери. Повний
посібник з автоматичним HTTPS через Caddy — у
[`server/README.md`](server/README.md). Потім на консолі:
**Налаштування → Сервер → Додати сервер**.

## Розробка

- Застосунок: [`client/README.md`](client/README.md) — збірка для PS5 за
  допомогою [ps5-payload-sdk](https://github.com/ps5-payload-dev/sdk) і
  десктопна збірка із SDL2 з Homebrew, керована файлом команд, щоб тестувати
  інтерфейс без консолі. Переклади зберігаються в `client/i18n/` (один JSON на
  мову).
- Сервер: [`server/README.md`](server/README.md) — Node.js ≥ 20 і PostgreSQL,
  наскрізні тести через `npm test`.
- Архітектура: [`docs/architecture.md`](docs/architecture.md).

Внески вітаються: див. [CONTRIBUTING.md](CONTRIBUTING.md).

## Автор

Omega розробляє **TheCriicom** — [outlinedigital.it](https://outlinedigital.it).

## Ліцензія

Copyright © 2026 TheCriicom і контриб'ютори Omega.
Omega — вільне програмне забезпечення: [GNU GPL v3 або новіша](LICENSE).
Сторонні компоненти та їхні ліцензії перелічено в
[`client/THIRD-PARTY-NOTICES.md`](client/THIRD-PARTY-NOTICES.md).

Omega — незалежний проєкт, не пов'язаний із Sony Interactive Entertainment,
не схвалений і не спонсорований нею. «PlayStation» і «PS5» — торговельні марки
відповідних власників. Omega не містить і не розповсюджує ігор.
