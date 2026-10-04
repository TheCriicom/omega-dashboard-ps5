<p align="center">
  <picture><source media="(prefers-color-scheme: dark)" srcset="docs/img/omega-mark.svg"><img src="docs/img/omega-mark-light.svg" width="96" alt="Omega"></picture>
</p>

<h1 align="center">Omega</h1>

<p align="center">
  Open-source nástěnka pro konzole PS5 s podporou homebrew: hry, Store, hudba, přátelé a party na jednom místě.<br>
  Vyvíjí <a href="https://outlinedigital.it"><b>TheCriicom</b></a>.
</p>

<p align="center"><sub><a href="README.md">English</a> · <a href="README.it.md">Italiano</a> · <a href="README.ja.md">日本語</a> · <a href="README.fr.md">Français</a> · <a href="README.es.md">Español</a> · <a href="README.de.md">Deutsch</a> · <a href="README.nl.md">Nederlands</a> · <a href="README.pt-PT.md">Português (Portugal)</a> · <a href="README.pt-BR.md">Português (Brasil)</a> · <a href="README.ru.md">Русский</a> · <a href="README.ko.md">한국어</a> · <a href="README.zh-Hans.md">简体中文</a> · <a href="README.zh-Hant.md">繁體中文</a> · <a href="README.fi.md">Suomi</a> · <a href="README.sv.md">Svenska</a> · <a href="README.da.md">Dansk</a> · <a href="README.nb.md">Norsk bokmål</a> · <a href="README.pl.md">Polski</a> · <a href="README.tr.md">Türkçe</a> · <b>Čeština</b> · <a href="README.hu.md">Magyar</a> · <a href="README.el.md">Ελληνικά</a> · <a href="README.ro.md">Română</a> · <a href="README.th.md">ไทย</a> · <a href="README.vi.md">Tiếng Việt</a> · <a href="README.id.md">Bahasa Indonesia</a> · <a href="README.uk.md">Українська</a></sub></p>

<p align="center">
  <a href="https://play.omegasuite.it">Web</a> ·
  <a href="https://play.omegasuite.it/installa">Instalace</a> ·
  <a href="server/README.md">Vlastní server</a> ·
  <a href="client/README.md">Vývoj</a>
</p>

![Obrazovka Home v Omega](docs/screenshots/home.png)

Omega je nástěnka navržená pro uživatele homebrew a pokud chcete, i vaše
obrazovka Home (zeptá se při prvním spuštění): nainstalované hry i homebrew jsou
v jedné řadě, Store instaluje jedním tlačítkem, hudba hraje dál i při hraní a
přátelé jsou vždy na dosah. Běží jako homebrew (SDL2, softwarové vykreslování)
a komunikuje se serverem, který může provozovat kdokoli.

## Co obsahuje

- **Home** s hrami i homebrew pohromadě, dynamickými pozadími z obrázků her a
  přímým spouštěním (hry přes LncUtil, homebrew přes websrv, ELF payloady na
  pozadí).
- **Store** s open-source homebrew: vyhledávání, police podle kategorií, hlasy,
  hodnocení a komentáře. Sám rozpozná `.pkg`, `.zip` a `.elf` a každý soubor
  nainstaluje na správné místo.
- **Moje knihovna**: zálohy vlastních her s odkazem ke stažení a obalem,
  přidávané z konzole nebo z telefonu, případně importované ze souboru JSON
  (jednorázově, nebo propojené, aby zůstaly synchronizované). Uložené pouze v
  konzoli.
- **Hudba**, i během hraní: internetové rádio (radio-browser), Navidrome a další
  servery Subsonic, soubory z USB, jakýkoli odkaz na audio. Přehrávání běží v
  daemonu na pozadí (sestavení FFmpeg jen pro audio), takže se nezastaví, když
  spustíte hru.
- **Ovladač v telefonu**: daemon poskytuje webovou stránku na portu 9095.
  Naskenujte QR kód zobrazený na konzoli, zadejte PIN a ovládejte hudbu,
  posílejte audio soubory a spravujte Moji knihovnu z prohlížeče v libovolném
  telefonu, tabletu nebo PC.
- **Rozpoznání HEN**: při spuštění Omega pozná OnionHEN, etaHEN, pldmgr nebo
  ps5_autoloader, upozorní, co chybí, a po vašem potvrzení nainstaluje a zapne
  jejich služby na správném místě. V OnionHEN přidá do herní nabídky (L2 + R3)
  stránku s ovládáním hudby.
- **Systémové nástroje**: teploty, práh ventilátoru, úložiště a správce souborů.
- **Community**: zeď s příspěvky, lajky a komentáři; skupinové chaty; lidé,
  které možná znáte; herní čas a žebříček přátel.
- **Party** s chatem a hlasem (Opus), pozvánkami do hry a vlastním stavem
  (online, nepřítomen, nerušit, neviditelný).
- **Game Base**: přátelé, žádosti a zprávy.
- **Soukromí**: blokování, nahlašování, kdo vám může psát, export dat, smazání
  účtu.
- **Motivy**, generovaná ambientní hudba, prohlížeč pro čtení.
- **27 jazyků**: aplikace automaticky přebírá jazyk konzole (lze ho změnit v
  Nastavení).
- **Volba serveru**: v Nastavení můžete přidat adresu libovolného serveru Omega;
  oficiální server zůstává vždy k dispozici.

| | |
|---|---|
| ![Store](docs/screenshots/store.png) | ![Community](docs/screenshots/community.png) |
| ![Party](docs/screenshots/party.png) | ![Herní čas](docs/screenshots/stats.png) |
| ![Ovladač v telefonu na PC](docs/screenshots/remote-music.png) | ![Moje knihovna z PC](docs/screenshots/remote-library.png) |

## Struktura repozitáře

| Složka | |
|---|---|
| [`client/`](client) | aplikace pro konzoli (C, SDL2) a desktopové sestavení pro vývoj na Macu |
| [`daemon/`](daemon) | payload na pozadí: přehrávač hudby, webová stránka ovladače v telefonu, Moje knihovna, oznámení během hraní a (jen pokud si to zvolíte) znovuotevření Omega po návratu na Home |
| [`onionhen-plugin/`](onionhen-plugin) | plugin pro OnionHEN: stránka Omega v herní nabídce s ovládáním hudby |
| [`server/`](server) | API v Node.js, proxy, moderátorský panel a Docker Compose pro provoz serveru |
| [`docs/`](docs) | architektura a obrázky |

## Instalace Omega na konzoli

Potřebujete PS5 s jailbreakem a podporou homebrew: HEN, například OnionHEN nebo
etaHEN, nebo spouštěč jako [websrv](https://github.com/ps5-payload-dev/websrv) s loaderem payloadů.
Nejjednodušší je otevřít v prohlížeči konzole stránku
[play.omegasuite.it/installa](https://play.omegasuite.it/installa).
Případně si stáhněte balíček z webu a zkopírujte `data/` do konzole přes FTP.

## Provoz serveru

```sh
git clone https://github.com/CristianLaporta/omega-dashboard-ps5.git
cd omega-dashboard-ps5/server
./scripts/install.sh
```

Skript vytvoří `.env` s náhodnými tajnými klíči a spustí kontejnery. Kompletní
návod včetně automatického HTTPS přes Caddy najdete v
[`server/README.md`](server/README.md). Poté na konzoli:
**Nastavení → Server → Přidat server**.

## Vývoj

- Aplikace: [`client/README.md`](client/README.md) — sestavení pro PS5 pomocí
  [ps5-payload-sdk](https://github.com/ps5-payload-dev/sdk) a desktopové
  sestavení s SDL2 z Homebrew, ovládané souborem příkazů, takže lze rozhraní
  testovat bez konzole. Překlady jsou v `client/i18n/` (jeden JSON na jazyk).
- Server: [`server/README.md`](server/README.md) — Node.js ≥ 20 a PostgreSQL,
  end-to-end testy přes `npm test`.
- Architektura: [`docs/architecture.md`](docs/architecture.md).

Příspěvky jsou vítány: viz [CONTRIBUTING.md](CONTRIBUTING.md).

## Autor

Omega vyvíjí **TheCriicom** — [outlinedigital.it](https://outlinedigital.it).

## Licence

Copyright © 2026 TheCriicom a přispěvatelé projektu Omega.
Omega je svobodný software: [GNU GPL verze 3 nebo novější](LICENSE). Komponenty
třetích stran a jejich licence jsou uvedeny v
[`client/THIRD-PARTY-NOTICES.md`](client/THIRD-PARTY-NOTICES.md).

Omega je nezávislý projekt a není přidružen ke společnosti Sony Interactive
Entertainment, ani jí schválen či sponzorován. „PlayStation“ a „PS5“ jsou
ochranné známky příslušných vlastníků. Omega neobsahuje ani nedistribuuje hry.
