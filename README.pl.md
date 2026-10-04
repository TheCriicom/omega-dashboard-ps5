<p align="center">
  <picture><source media="(prefers-color-scheme: dark)" srcset="docs/img/omega-mark.svg"><img src="docs/img/omega-mark-light.svg" width="96" alt="Omega"></picture>
</p>

<h1 align="center">Omega</h1>

<p align="center">
  Otwartoźródłowy pulpit dla konsol PS5 z obsługą homebrew: gry, Store, muzyka, znajomi i party w jednym miejscu.<br>
  Tworzony przez <a href="https://outlinedigital.it"><b>TheCriicom</b></a>.
</p>

<p align="center"><sub><a href="README.md">English</a> · <a href="README.it.md">Italiano</a> · <a href="README.ja.md">日本語</a> · <a href="README.fr.md">Français</a> · <a href="README.es.md">Español</a> · <a href="README.de.md">Deutsch</a> · <a href="README.nl.md">Nederlands</a> · <a href="README.pt-PT.md">Português (Portugal)</a> · <a href="README.pt-BR.md">Português (Brasil)</a> · <a href="README.ru.md">Русский</a> · <a href="README.ko.md">한국어</a> · <a href="README.zh-Hans.md">简体中文</a> · <a href="README.zh-Hant.md">繁體中文</a> · <a href="README.fi.md">Suomi</a> · <a href="README.sv.md">Svenska</a> · <a href="README.da.md">Dansk</a> · <a href="README.nb.md">Norsk bokmål</a> · <b>Polski</b> · <a href="README.tr.md">Türkçe</a> · <a href="README.cs.md">Čeština</a> · <a href="README.hu.md">Magyar</a> · <a href="README.el.md">Ελληνικά</a> · <a href="README.ro.md">Română</a> · <a href="README.th.md">ไทย</a> · <a href="README.vi.md">Tiếng Việt</a> · <a href="README.id.md">Bahasa Indonesia</a> · <a href="README.uk.md">Українська</a></sub></p>

<p align="center">
  <a href="https://play.omegasuite.it">Strona</a> ·
  <a href="https://play.omegasuite.it/installa">Instalacja</a> ·
  <a href="server/README.md">Własny serwer</a> ·
  <a href="client/README.md">Rozwój</a>
</p>

![Ekran Home w Omega](docs/screenshots/home.png)

Omega to pulpit stworzony z myślą o użytkownikach homebrew, a jeśli chcesz, także
twój ekran Home (zapyta o to przy pierwszym uruchomieniu): zainstalowane gry i
homebrew są w jednym rzędzie, Store instaluje jednym przyciskiem, muzyka gra dalej,
gdy grasz, a znajomi są zawsze na wyciągnięcie ręki. Działa jako homebrew (SDL2,
renderowanie programowe) i komunikuje się z serwerem, który może uruchomić każdy.

## Co jest w środku

- **Home** z grami i homebrew razem, dynamicznymi tłami z grafik i
  bezpośrednim uruchamianiem (gry przez LncUtil, homebrew przez websrv, payloady
  ELF w tle).
- **Store** z otwartoźródłowymi homebrew: wyszukiwanie, półki kategorii, głosy,
  oceny i komentarze. Sam rozpoznaje pliki `.pkg`, `.zip` i `.elf` i instaluje
  każdy z nich we właściwym miejscu.
- **Moja biblioteka**: własne kopie zapasowe gier z linkiem do pobrania i okładką,
  dodawane z konsoli lub z telefonu albo importowane z pliku JSON (jednorazowo albo
  jako powiązane, żeby stale się synchronizowały). Przechowywane tylko na konsoli.
- **Muzyka**, także podczas gier: radio internetowe (radio-browser), Navidrome i
  inne serwery Subsonic, pliki z USB, dowolny link do audio. Odtwarzanie działa w
  daemonie w tle (wersja FFmpeg tylko z dźwiękiem), więc nie przerywa się po
  uruchomieniu gry.
- **Pilot w telefonie**: daemon udostępnia stronę WWW na porcie 9095. Zeskanuj kod
  QR widoczny na konsoli, wpisz PIN i steruj muzyką, wysyłaj pliki audio i
  zarządzaj Moją biblioteką z przeglądarki na dowolnym telefonie, tablecie lub
  komputerze.
- **Wykrywanie HEN**: przy starcie Omega rozpoznaje OnionHEN, etaHEN, pldmgr lub
  ps5_autoloader, mówi, czego brakuje, i po twoim potwierdzeniu instaluje oraz
  włącza jego usługi we właściwym miejscu. W OnionHEN dodaje do menu w grze
  (L2 + R3) stronę ze sterowaniem muzyką.
- **Narzędzia systemowe**: temperatury, próg wentylatora, pamięć i menedżer plików.
- **Community**: tablica z postami, polubieniami i komentarzami; czaty grupowe;
  osoby, które możesz znać; czas gry i ranking znajomych.
- **Party** z czatem i głosem (Opus), zaproszeniami do gry i własnym statusem
  (dostępny, zaraz wracam, nie przeszkadzać, niewidoczny).
- **Game Base**: znajomi, zaproszenia i wiadomości.
- **Prywatność**: blokowanie, zgłaszanie, kto może do ciebie pisać, eksport
  danych, usunięcie konta.
- **Motywy**, generowana muzyka w tle, przeglądarka do czytania.
- **27 języków**: aplikacja automatycznie używa języka konsoli (można go
  zmienić w Ustawieniach).
- **Wybór serwera**: w Ustawieniach możesz dodać adres dowolnego serwera Omega;
  oficjalny serwer pozostaje zawsze dostępny.

| | |
|---|---|
| ![Store](docs/screenshots/store.png) | ![Community](docs/screenshots/community.png) |
| ![Party](docs/screenshots/party.png) | ![Czas gry](docs/screenshots/stats.png) |
| ![Pilot w telefonie na komputerze](docs/screenshots/remote-music.png) | ![Moja biblioteka z komputera](docs/screenshots/remote-library.png) |

## Struktura repozytorium

| Folder | |
|---|---|
| [`client/`](client) | aplikacja na konsolę (C, SDL2) i wersja desktopowa do pracy nad nią na Macu |
| [`daemon/`](daemon) | payload działający w tle: odtwarzacz muzyki, strona pilota w telefonie, Moja biblioteka, powiadomienia w trakcie gry oraz (tylko jeśli tak wybierzesz) ponowne otwieranie Omega po powrocie do ekranu Home |
| [`onionhen-plugin/`](onionhen-plugin) | wtyczka OnionHEN: strona Omega w menu w grze ze sterowaniem muzyką |
| [`server/`](server) | API w Node.js, proxy, panel moderacji i Docker Compose do uruchomienia serwera |
| [`docs/`](docs) | architektura i obrazy |

## Instalacja Omega na konsoli

Potrzebujesz PS5 z jailbreakiem i obsługą homebrew: HEN-a, takiego jak OnionHEN
lub etaHEN, albo launchera, takiego jak [websrv](https://github.com/ps5-payload-dev/websrv), wraz z loaderem payloadów.
Najprościej otworzyć w przeglądarce konsoli stronę
[play.omegasuite.it/installa](https://play.omegasuite.it/installa).
Możesz też pobrać pakiet ze strony i skopiować `data/` na konsolę przez FTP.

## Uruchamianie serwera

```sh
git clone https://github.com/TheCriicom/omega-dashboard-ps5.git
cd omega-dashboard-ps5/server
./scripts/install.sh
```

Skrypt tworzy plik `.env` z losowymi sekretami i uruchamia kontenery. Pełny
przewodnik, z automatycznym HTTPS przez Caddy, znajdziesz w
[`server/README.md`](server/README.md). Następnie na konsoli:
**Ustawienia → Serwer → Dodaj serwer**.

## Rozwój

- Aplikacja: [`client/README.md`](client/README.md) — kompilacja na PS5 z
  [ps5-payload-sdk](https://github.com/ps5-payload-dev/sdk) oraz wersja
  desktopowa z SDL2 z Homebrew, sterowana plikiem poleceń, by testować interfejs
  bez konsoli. Tłumaczenia znajdują się w `client/i18n/` (jeden JSON na język).
- Serwer: [`server/README.md`](server/README.md) — Node.js ≥ 20 i PostgreSQL,
  testy end-to-end przez `npm test`.
- Architektura: [`docs/architecture.md`](docs/architecture.md).

Wkład w projekt jest mile widziany: zobacz [CONTRIBUTING.md](CONTRIBUTING.md).

## Autor

Omega jest tworzona przez **TheCriicom** — [outlinedigital.it](https://outlinedigital.it).

## Licencja

Copyright © 2026 TheCriicom i współtwórcy Omega.
Omega jest wolnym oprogramowaniem: [GNU GPL w wersji 3 lub późniejszej](LICENSE).
Komponenty zewnętrzne i ich licencje są wymienione w
[`client/THIRD-PARTY-NOTICES.md`](client/THIRD-PARTY-NOTICES.md).

Omega jest niezależnym projektem i nie jest powiązana z Sony Interactive
Entertainment, ani przez nią zatwierdzona czy sponsorowana. „PlayStation” i
„PS5” są znakami towarowymi ich odpowiednich właścicieli. Omega nie zawiera ani
nie rozpowszechnia gier.
