<p align="center">
  <picture><source media="(prefers-color-scheme: dark)" srcset="docs/img/omega-mark.svg"><img src="docs/img/omega-mark-light.svg" width="96" alt="Omega"></picture>
</p>

<h1 align="center">Omega</h1>

<p align="center">
  Avoimen lähdekoodin kojelauta homebrew-yhteensopiville PS5-konsoleille: pelit, Store, musiikki, kaverit ja party yhdessä paikassa.<br>
  Kehittäjä: <a href="https://outlinedigital.it"><b>TheCriicom</b></a>.
</p>

<p align="center"><sub><a href="README.md">English</a> · <a href="README.it.md">Italiano</a> · <a href="README.ja.md">日本語</a> · <a href="README.fr.md">Français</a> · <a href="README.es.md">Español</a> · <a href="README.de.md">Deutsch</a> · <a href="README.nl.md">Nederlands</a> · <a href="README.pt-PT.md">Português (Portugal)</a> · <a href="README.pt-BR.md">Português (Brasil)</a> · <a href="README.ru.md">Русский</a> · <a href="README.ko.md">한국어</a> · <a href="README.zh-Hans.md">简体中文</a> · <a href="README.zh-Hant.md">繁體中文</a> · <b>Suomi</b> · <a href="README.sv.md">Svenska</a> · <a href="README.da.md">Dansk</a> · <a href="README.nb.md">Norsk bokmål</a> · <a href="README.pl.md">Polski</a> · <a href="README.tr.md">Türkçe</a> · <a href="README.cs.md">Čeština</a> · <a href="README.hu.md">Magyar</a> · <a href="README.el.md">Ελληνικά</a> · <a href="README.ro.md">Română</a> · <a href="README.th.md">ไทย</a> · <a href="README.vi.md">Tiếng Việt</a> · <a href="README.id.md">Bahasa Indonesia</a> · <a href="README.uk.md">Українська</a></sub></p>

<p align="center">
  <a href="https://play.omegasuite.it">Verkkosivusto</a> ·
  <a href="https://play.omegasuite.it/installa">Asennus</a> ·
  <a href="server/README.md">Oma palvelin</a> ·
  <a href="client/README.md">Kehitys</a>
</p>

![Omegan Home](docs/screenshots/home.png)

Omega on homebrew-käyttäjille tehty kojelauta, ja halutessasi myös Home-näkymäsi
(se kysyy asiaa ensimmäisellä käynnistyskerralla): asennetut pelit ja
homebrew-ohjelmat ovat samalla rivillä, Store asentaa yhdellä painalluksella,
musiikki jatkaa soimistaan pelatessasi ja kaverit ovat aina yhden napinpainalluksen
päässä. Omega toimii homebrew-ohjelmana (SDL2, ohjelmistorenderöijä) ja keskustelee
palvelimen kanssa, jota kuka tahansa voi ylläpitää.

## Mitä sisältä löytyy

- **Home**, jossa pelit ja homebrew-ohjelmat ovat yhdessä, kansikuvista luodut
  dynaamiset taustat ja suora käynnistys (pelit LncUtilin kautta, homebrew-ohjelmat
  websrv:n kautta, ELF-payloadit taustalla).
- Avoimen lähdekoodin homebrew-ohjelmien **Store**: haku, kategoriahyllyt, äänet,
  arviot ja kommentit. Se tunnistaa `.pkg`-, `.zip`- ja `.elf`-tiedostot itse ja
  asentaa jokaisen oikeaan paikkaan.
- **Oma kirjasto**: omien pelien varmuuskopiot latauslinkin ja kansikuvan kanssa,
  lisättynä konsolista tai puhelimesta tai tuotuna JSON-tiedostosta (kerran tai
  linkitettynä, jolloin se pysyy ajan tasalla). Tallennetaan vain konsoliin.
- **Musiikki**, myös pelien aikana: verkkoradio (radio-browser), Navidrome ja muut
  Subsonic-palvelimet, USB-tiedostot, mikä tahansa äänilinkki. Toisto toimii
  taustalla pyörivässä daemonissa (FFmpegin vain ääntä sisältävä käännös), joten se
  ei katkea, kun käynnistät pelin.
- **Puhelinkaukosäädin**: daemon tarjoaa verkkosivun portissa 9095. Skannaa
  konsolin näyttämä QR-koodi, kirjoita PIN ja ohjaa musiikkia, lähetä äänitiedostoja
  ja hallitse Omaa kirjastoa minkä tahansa puhelimen, tabletin tai tietokoneen
  selaimella.
- **HEN-tunnistus**: käynnistyksessä Omega tunnistaa OnionHENin, etaHENin,
  pldmgr:n tai ps5_autoloaderin, kertoo mitä puuttuu ja asentaa vahvistuksesi
  jälkeen sen palvelut ja ottaa ne käyttöön oikeaan paikkaan. OnionHENillä se
  lisää pelinaikaiseen valikkoon (L2 + R3) sivun musiikin ohjaimilla.
- **Järjestelmätyökalut**: lämpötilat, tuulettimen kynnysarvo, tallennustila ja
  tiedostonhallinta.
- **Community**: seinä julkaisuineen, tykkäyksineen ja kommentteineen;
  ryhmäkeskustelut; ihmisiä, jotka saatat tuntea; peliaika ja kavereiden tulostaulukko.
- **Party** tekstikeskustelulla ja äänellä (Opus), pelikutsut ja mukautettu tila
  (paikalla, poissa, älä häiritse, näkymätön).
- **Game Base**: kaverit, kaveripyynnöt ja viestit.
- **Yksityisyys**: estäminen, ilmoittaminen, kuka voi lähettää sinulle viestejä,
  tietojen vienti ja tilin poistaminen.
- **Teemat**, generoitu taustamusiikki ja lukuselain.
- **27 kieltä**: sovellus käyttää automaattisesti konsolin kieltä (ja kielen voi
  vaihtaa Asetuksista).
- **Valitse palvelimesi**: lisää minkä tahansa Omega-palvelimen osoite Asetuksissa;
  virallinen palvelin on aina käytettävissä.

| | |
|---|---|
| ![Store](docs/screenshots/store.png) | ![Community](docs/screenshots/community.png) |
| ![Party](docs/screenshots/party.png) | ![Peliaika](docs/screenshots/stats.png) |
| ![Puhelinkaukosäädin tietokoneella](docs/screenshots/remote-music.png) | ![Oma kirjasto tietokoneelta](docs/screenshots/remote-library.png) |

## Repositorion rakenne

| Kansio | |
|---|---|
| [`client/`](client) | konsolisovellus (C, SDL2) ja työpöytäversio sen kehittämiseen Macilla |
| [`daemon/`](daemon) | taustapayload: musiikkisoitin, puhelinkaukosäätimen verkkosivu, Oma kirjasto, ilmoitukset pelien aikana ja (vain jos valitset sen) Omegan avaaminen uudelleen, kun palaat Homeen |
| [`onionhen-plugin/`](onionhen-plugin) | OnionHEN-lisäosa: Omega-sivu pelinaikaisessa valikossa musiikin ohjaimilla |
| [`server/`](server) | Node.js-API, välityspalvelin, moderointipaneeli ja Docker Compose palvelimen ylläpitämiseen |
| [`docs/`](docs) | arkkitehtuuri ja kuvat |

## Omegan asentaminen konsoliin

Tarvitset jailbreakatun PS5:n, jossa on homebrew-tuki: HEN, kuten OnionHEN tai
etaHEN, tai käynnistin, kuten [websrv](https://github.com/ps5-payload-dev/websrv), ja payload-lataaja.
Helpoin tapa on avata [play.omegasuite.it/installa](https://play.omegasuite.it/installa)
konsolin selaimessa. Vaihtoehtoisesti voit ladata paketin verkkosivustolta ja
kopioida `data/`-kansion konsoliin FTP:llä.

## Oman palvelimen ylläpitäminen

```sh
git clone https://github.com/TheCriicom/omega-dashboard-ps5.git
cd omega-dashboard-ps5/server
./scripts/install.sh
```

Skripti luo `.env`-tiedoston satunnaisilla salaisuuksilla ja käynnistää kontit.
Täydellinen opas, johon sisältyy automaattinen HTTPS Caddyn avulla, on tiedostossa
[`server/README.md`](server/README.md). Valitse sen jälkeen konsolissa:
**Asetukset → Palvelin → Lisää palvelin**.

## Kehitys

- Sovellus: [`client/README.md`](client/README.md) — PS5-käännös
  [ps5-payload-sdk](https://github.com/ps5-payload-dev/sdk):lla ja työpöytäkäännös
  Homebrew'n SDL2:lla; työpöytäversiota ohjataan komentotiedostolla, joten
  käyttöliittymää voi testata ilman konsolia. Käännökset ovat kansiossa
  `client/i18n/` (yksi JSON kieltä kohden).
- Palvelin: [`server/README.md`](server/README.md) — Node.js ≥ 20 ja PostgreSQL,
  päästä päähän -testit komennolla `npm test`.
- Arkkitehtuuri: [`docs/architecture.md`](docs/architecture.md).

Kontribuutiot ovat tervetulleita: katso [CONTRIBUTING.md](CONTRIBUTING.md).

## Tekijä

Omegaa kehittää **TheCriicom** — [outlinedigital.it](https://outlinedigital.it).

## Lisenssi

Copyright © 2026 TheCriicom ja Omegan kontribuuttorit.
Omega on vapaa ohjelmisto: [GNU GPL v3 tai uudempi](LICENSE). Kolmannen osapuolen
komponentit ja niiden lisenssit on lueteltu tiedostossa
[`client/THIRD-PARTY-NOTICES.md`](client/THIRD-PARTY-NOTICES.md).

Omega on itsenäinen projekti, eikä se ole sidoksissa Sony Interactive
Entertainmentiin, eikä Sony Interactive Entertainment ole hyväksynyt tai
sponsoroinut sitä. "PlayStation" ja "PS5" ovat omistajiensa tavaramerkkejä.
Omega ei sisällä eikä levitä pelejä.
