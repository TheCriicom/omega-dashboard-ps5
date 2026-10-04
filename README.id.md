<p align="center">
  <picture><source media="(prefers-color-scheme: dark)" srcset="docs/img/omega-mark.svg"><img src="docs/img/omega-mark-light.svg" width="96" alt="Omega"></picture>
</p>

<h1 align="center">Omega</h1>

<p align="center">
  Dasbor open source untuk konsol PS5 yang mendukung homebrew: game, Store, musik, teman, dan party dalam satu tempat.<br>
  Dikembangkan oleh <a href="https://outlinedigital.it"><b>TheCriicom</b></a>.
</p>

<p align="center"><sub><a href="README.md">English</a> · <a href="README.it.md">Italiano</a> · <a href="README.ja.md">日本語</a> · <a href="README.fr.md">Français</a> · <a href="README.es.md">Español</a> · <a href="README.de.md">Deutsch</a> · <a href="README.nl.md">Nederlands</a> · <a href="README.pt-PT.md">Português (Portugal)</a> · <a href="README.pt-BR.md">Português (Brasil)</a> · <a href="README.ru.md">Русский</a> · <a href="README.ko.md">한국어</a> · <a href="README.zh-Hans.md">简体中文</a> · <a href="README.zh-Hant.md">繁體中文</a> · <a href="README.fi.md">Suomi</a> · <a href="README.sv.md">Svenska</a> · <a href="README.da.md">Dansk</a> · <a href="README.nb.md">Norsk bokmål</a> · <a href="README.pl.md">Polski</a> · <a href="README.tr.md">Türkçe</a> · <a href="README.cs.md">Čeština</a> · <a href="README.hu.md">Magyar</a> · <a href="README.el.md">Ελληνικά</a> · <a href="README.ro.md">Română</a> · <a href="README.th.md">ไทย</a> · <a href="README.vi.md">Tiếng Việt</a> · <b>Bahasa Indonesia</b> · <a href="README.uk.md">Українська</a></sub></p>

<p align="center">
  <a href="https://play.omegasuite.it">Situs web</a> ·
  <a href="https://play.omegasuite.it/installa">Instal</a> ·
  <a href="server/README.md">Host server sendiri</a> ·
  <a href="client/README.md">Pengembangan</a>
</p>

![Home Omega](docs/screenshots/home.png)

Omega adalah dasbor yang dirancang untuk pengguna homebrew, dan bisa menjadi
layar Home Anda jika Anda mau (Omega menanyakannya saat pertama kali dibuka):
game yang terinstal dan homebrew berada di baris yang sama, Store menginstal
dengan satu tombol, musik tetap menyala saat Anda bermain, dan teman-teman Anda
selalu dapat dijangkau dengan sekali tekan. Omega berjalan sebagai homebrew
(SDL2, perender perangkat lunak) dan terhubung ke server yang dapat di-host oleh
siapa saja.

## Isinya

- **Home** dengan game dan homebrew berdampingan, latar belakang dinamis dari
  artwork, peluncuran langsung (game lewat LncUtil, homebrew lewat websrv,
  payload ELF di latar belakang).
- **Store** homebrew open source: pencarian, rak per kategori, vote, rating,
  dan komentar. Store mengenali `.pkg`, `.zip`, dan `.elf` secara otomatis dan
  menginstal masing-masing di tempat yang tepat.
- **Pustaka saya**: cadangan game milik Anda sendiri, lengkap dengan tautan unduh
  dan sampul, ditambahkan dari konsol atau dari ponsel, atau diimpor dari file
  JSON (sekali impor, atau ditautkan agar tetap sinkron). Hanya disimpan di
  konsol.
- **Musik**, termasuk saat bermain: radio internet (radio-browser), Navidrome dan
  server Subsonic lainnya, file USB, tautan audio apa pun. Pemutaran berjalan di
  daemon latar belakang (build FFmpeg khusus audio), jadi tidak berhenti saat
  Anda membuka game.
- **Remote ponsel**: daemon menyajikan halaman web di port 9095. Pindai kode QR
  yang tampil di konsol, ketik PIN, lalu kendalikan musik, kirim file audio, dan
  kelola Pustaka saya dari browser ponsel, tablet, atau PC mana pun.
- **Deteksi HEN**: saat startup, Omega mengenali OnionHEN, etaHEN, pldmgr, atau
  ps5_autoloader, memberi tahu apa yang kurang, dan setelah Anda konfirmasi,
  menginstal serta mengaktifkan layanannya di tempat yang tepat. Di OnionHEN,
  Omega menambahkan satu halaman ke menu dalam game (L2 + R3) berisi kontrol
  musik.
- **Alat sistem**: suhu, ambang kipas, penyimpanan, dan pengelola file.
- **Community**: dinding berisi postingan, suka, dan komentar; obrolan grup;
  orang yang mungkin Anda kenal; waktu bermain dan papan peringkat teman.
- **Party** dengan obrolan teks dan suara (Opus), undangan bermain, status
  kustom (online, sedang pergi, jangan ganggu, tak terlihat).
- **Game Base**: teman, permintaan pertemanan, dan pesan.
- **Privasi**: blokir, laporkan, siapa yang dapat mengirimi Anda pesan, ekspor
  data, penghapusan akun.
- **Tema**, musik latar yang dihasilkan otomatis, browser untuk membaca.
- **27 bahasa**: aplikasi otomatis mengikuti bahasa konsol (dan dapat diubah
  di Pengaturan).
- **Pilih server Anda**: tambahkan alamat server Omega mana pun di Pengaturan;
  server resmi selalu tetap tersedia.

| | |
|---|---|
| ![Store](docs/screenshots/store.png) | ![Community](docs/screenshots/community.png) |
| ![Party](docs/screenshots/party.png) | ![Waktu bermain](docs/screenshots/stats.png) |
| ![Remote ponsel di PC](docs/screenshots/remote-music.png) | ![Pustaka saya dari PC](docs/screenshots/remote-library.png) |

## Struktur repositori

| Folder | |
|---|---|
| [`client/`](client) | aplikasi konsol (C, SDL2) dan build desktop untuk mengembangkannya di Mac |
| [`daemon/`](daemon) | payload latar belakang: pemutar musik, halaman web remote ponsel, Pustaka saya, notifikasi selama bermain, dan (hanya jika Anda memilihnya) membuka kembali Omega saat Anda kembali ke Home |
| [`onionhen-plugin/`](onionhen-plugin) | plugin OnionHEN: halaman Omega di menu dalam game dengan kontrol musik |
| [`server/`](server) | API Node.js, proxy, panel moderasi, dan Docker Compose untuk meng-host server |
| [`docs/`](docs) | arsitektur dan gambar |

## Menginstal Omega di konsol

Anda memerlukan PS5 yang sudah di-jailbreak dengan dukungan homebrew: HEN seperti
OnionHEN atau etaHEN, atau launcher seperti
[websrv](https://github.com/ps5-payload-dev/websrv) dengan payload loader.
Cara termudah adalah membuka [play.omegasuite.it/installa](https://play.omegasuite.it/installa)
di browser konsol. Sebagai alternatif, unduh paket dari situs web dan salin
`data/` ke konsol melalui FTP.

## Meng-host server

```sh
git clone https://github.com/CristianLaporta/omega-dashboard-ps5.git
cd omega-dashboard-ps5/server
./scripts/install.sh
```

Skrip ini membuat `.env` dengan secret acak dan menjalankan container. Panduan
lengkap, dengan HTTPS otomatis melalui Caddy, ada di
[`server/README.md`](server/README.md). Lalu, di konsol:
**Pengaturan → Server → Tambahkan server**.

## Pengembangan

- Aplikasi: [`client/README.md`](client/README.md) — build PS5 dengan
  [ps5-payload-sdk](https://github.com/ps5-payload-dev/sdk) dan build desktop
  dengan SDL2 dari Homebrew, dikendalikan lewat file perintah untuk menguji
  antarmuka tanpa konsol. Terjemahan berada di `client/i18n/` (satu JSON per
  bahasa).
- Server: [`server/README.md`](server/README.md) — Node.js ≥ 20 dan PostgreSQL,
  pengujian end-to-end dengan `npm test`.
- Arsitektur: [`docs/architecture.md`](docs/architecture.md).

Kontribusi dipersilakan: lihat [CONTRIBUTING.md](CONTRIBUTING.md).

## Pembuat

Omega dikembangkan oleh **TheCriicom** — [outlinedigital.it](https://outlinedigital.it).

## Lisensi

Copyright © 2026 TheCriicom dan para kontributor Omega.
Omega adalah perangkat lunak bebas: [GNU GPL v3 atau yang lebih baru](LICENSE).
Komponen pihak ketiga dan lisensinya tercantum di
[`client/THIRD-PARTY-NOTICES.md`](client/THIRD-PARTY-NOTICES.md).

Omega adalah proyek independen dan tidak berafiliasi dengan, didukung, atau
disponsori oleh Sony Interactive Entertainment. "PlayStation" dan "PS5" adalah
merek dagang dari pemiliknya masing-masing. Omega tidak berisi dan tidak
mendistribusikan game.
