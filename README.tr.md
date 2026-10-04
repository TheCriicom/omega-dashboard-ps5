<p align="center">
  <picture><source media="(prefers-color-scheme: dark)" srcset="docs/img/omega-mark.svg"><img src="docs/img/omega-mark-light.svg" width="96" alt="Omega"></picture>
</p>

<h1 align="center">Omega</h1>

<p align="center">
  Homebrew destekli PS5 konsolları için açık kaynaklı panel: oyunlar, Store, müzik, arkadaşlar ve party tek bir yerde.<br>
  Geliştiren: <a href="https://outlinedigital.it"><b>TheCriicom</b></a>.
</p>

<p align="center"><sub><a href="README.md">English</a> · <a href="README.it.md">Italiano</a> · <a href="README.ja.md">日本語</a> · <a href="README.fr.md">Français</a> · <a href="README.es.md">Español</a> · <a href="README.de.md">Deutsch</a> · <a href="README.nl.md">Nederlands</a> · <a href="README.pt-PT.md">Português (Portugal)</a> · <a href="README.pt-BR.md">Português (Brasil)</a> · <a href="README.ru.md">Русский</a> · <a href="README.ko.md">한국어</a> · <a href="README.zh-Hans.md">简体中文</a> · <a href="README.zh-Hant.md">繁體中文</a> · <a href="README.fi.md">Suomi</a> · <a href="README.sv.md">Svenska</a> · <a href="README.da.md">Dansk</a> · <a href="README.nb.md">Norsk bokmål</a> · <a href="README.pl.md">Polski</a> · <b>Türkçe</b> · <a href="README.cs.md">Čeština</a> · <a href="README.hu.md">Magyar</a> · <a href="README.el.md">Ελληνικά</a> · <a href="README.ro.md">Română</a> · <a href="README.th.md">ไทย</a> · <a href="README.vi.md">Tiếng Việt</a> · <a href="README.id.md">Bahasa Indonesia</a> · <a href="README.uk.md">Українська</a></sub></p>

<p align="center">
  <a href="https://play.omegasuite.it">Web sitesi</a> ·
  <a href="https://play.omegasuite.it/installa">Kurulum</a> ·
  <a href="server/README.md">Sunucu barındır</a> ·
  <a href="client/README.md">Geliştirme</a>
</p>

![Omega Home ekranı](docs/screenshots/home.png)

Omega, homebrew kullanıcıları için tasarlanmış bir paneldir; istersen Home
ekranın da olur (ilk açılışta sorar): yüklü oyunlar ve homebrew'lar aynı sırada
durur, Store tek tuşla kurulum yapar, oyun oynarken müzik çalmaya devam eder ve
arkadaşların her zaman bir tuş uzağındadır. Homebrew olarak çalışır (SDL2,
yazılımsal oluşturucu) ve herkesin barındırabileceği bir sunucuyla konuşur.

## Neler var

- Oyunları ve homebrew'ları bir arada gösteren **Home**; görsellerden üretilen
  dinamik arka planlar, doğrudan başlatma (oyunlar LncUtil ile, homebrew'lar
  websrv ile, ELF payload'ları arka planda).
- Açık kaynaklı homebrew'lardan oluşan **Store**: arama, kategori rafları, oylar,
  puanlar ve yorumlar. `.pkg`, `.zip` ve `.elf` dosyalarını kendisi tanır ve her
  birini doğru yere kurar.
- **Kütüphanem**: sahip olduğun oyunların yedekleri; indirme bağlantısı ve
  kapak görseliyle, konsoldan ya da telefondan eklenir veya bir JSON dosyasından
  içe aktarılır (bir kez ya da senkron kalacak şekilde bağlanarak). Yalnızca
  konsolda saklanır.
- Oyunlar sırasında da çalan **Müzik**: internet radyosu (radio-browser),
  Navidrome ve diğer Subsonic sunucuları, USB dosyaları, herhangi bir ses
  bağlantısı. Çalma, arka plandaki daemon'da (yalnızca ses içeren FFmpeg
  derlemesi) yürür; bu yüzden bir oyunu açtığında durmaz.
- **Telefon kumandası**: daemon, 9095 portunda bir web sayfası sunar. Konsolda
  görünen QR kodunu tara, PIN'i gir; müziği kontrol et, ses dosyaları gönder ve
  Kütüphanem'i herhangi bir telefon, tablet ya da bilgisayar tarayıcısından
  yönet.
- **HEN algılama**: Omega açılışta OnionHEN, etaHEN, pldmgr veya
  ps5_autoloader'ı tanır, nelerin eksik olduğunu söyler ve onayladıktan sonra
  servislerini doğru yere kurup etkinleştirir. OnionHEN'de oyun içi menüye
  (L2 + R3) müzik kontrolleri olan bir sayfa ekler.
- **Sistem araçları**: sıcaklıklar, fan eşiği, depolama ve bir dosya yöneticisi.
- **Community**: gönderiler, beğeniler ve yorumlarla bir duvar; grup sohbetleri;
  tanıyor olabileceğin kişiler; oyun süresi ve arkadaş sıralaması.
- Sohbet ve sesli görüşmeli (Opus) **Party**, oyun davetleri, özel durum
  (çevrimiçi, uzakta, rahatsız etmeyin, görünmez).
- **Game Base**: arkadaşlar, istekler ve mesajlar.
- **Gizlilik**: engelleme, şikâyet etme, sana kimlerin mesaj atabileceği, veri
  dışa aktarma, hesap silme.
- **Temalar**, üretilmiş ortam müziği, okuma tarayıcısı.
- **27 dil**: uygulama konsolun dilini otomatik olarak kullanır (Ayarlar'dan
  değiştirilebilir).
- **Sunucunu seç**: Ayarlar'dan herhangi bir Omega sunucusunun adresini
  ekleyebilirsin; resmî sunucu her zaman kullanılabilir kalır.

| | |
|---|---|
| ![Store](docs/screenshots/store.png) | ![Community](docs/screenshots/community.png) |
| ![Party](docs/screenshots/party.png) | ![Oyun süresi](docs/screenshots/stats.png) |
| ![Bilgisayarda telefon kumandası](docs/screenshots/remote-music.png) | ![Bilgisayardan Kütüphanem](docs/screenshots/remote-library.png) |

## Depo yapısı

| Klasör | |
|---|---|
| [`client/`](client) | konsol uygulaması (C, SDL2) ve onu Mac'te geliştirmek için masaüstü derlemesi |
| [`daemon/`](daemon) | arka plan payload'ı: müzik çalar, telefon kumandası web sayfası, Kütüphanem, oyun sırasında bildirimler ve (yalnızca sen seçersen) Home ekranına dönüldüğünde Omega'yı yeniden açma |
| [`onionhen-plugin/`](onionhen-plugin) | OnionHEN eklentisi: oyun içi menüde müzik kontrolleri olan bir Omega sayfası |
| [`server/`](server) | Node.js API, proxy, moderasyon paneli ve sunucu barındırmak için Docker Compose |
| [`docs/`](docs) | mimari ve görseller |

## Omega'yı konsola kurmak

Homebrew desteği olan jailbreak'li bir PS5 gerekir: OnionHEN ya da etaHEN gibi
bir HEN veya payload yükleyicili [websrv](https://github.com/ps5-payload-dev/websrv) gibi bir
başlatıcı. En kolay yol, konsolun tarayıcısında
[play.omegasuite.it/installa](https://play.omegasuite.it/installa) sayfasını
açmaktır. Alternatif olarak paketi web sitesinden indirip `data/` klasörünü FTP
ile konsola kopyalayabilirsin.

## Sunucu barındırmak

```sh
git clone https://github.com/TheCriicom/omega-dashboard-ps5.git
cd omega-dashboard-ps5/server
./scripts/install.sh
```

Betik, rastgele gizli anahtarlarla `.env` dosyasını oluşturur ve konteynerleri
başlatır. Caddy ile otomatik HTTPS dahil tam kılavuz
[`server/README.md`](server/README.md) dosyasındadır. Ardından konsolda:
**Ayarlar → Sunucu → Sunucu ekle**.

## Geliştirme

- Uygulama: [`client/README.md`](client/README.md) —
  [ps5-payload-sdk](https://github.com/ps5-payload-dev/sdk) ile PS5 derlemesi ve
  arayüzü konsol olmadan denemek için bir komut dosyasıyla yönetilen, Homebrew'un
  SDL2'si ile masaüstü derlemesi. Çeviriler `client/i18n/` içindedir (her dil
  için bir JSON).
- Sunucu: [`server/README.md`](server/README.md) — Node.js ≥ 20 ve PostgreSQL,
  `npm test` ile uçtan uca testler.
- Mimari: [`docs/architecture.md`](docs/architecture.md).

Katkılarınızı bekliyoruz: [CONTRIBUTING.md](CONTRIBUTING.md) dosyasına bakın.

## Geliştirici

Omega, **TheCriicom** tarafından geliştirilmektedir — [outlinedigital.it](https://outlinedigital.it).

## Lisans

Copyright © 2026 TheCriicom ve Omega katkıda bulunanları.
Omega özgür bir yazılımdır: [GNU GPL v3 veya sonrası](LICENSE). Üçüncü taraf
bileşenler ve lisansları
[`client/THIRD-PARTY-NOTICES.md`](client/THIRD-PARTY-NOTICES.md) dosyasında
listelenmiştir.

Omega bağımsız bir projedir; Sony Interactive Entertainment ile bağlantılı
değildir, onun tarafından onaylanmamış veya desteklenmemiştir. "PlayStation" ve
"PS5" ilgili sahiplerinin ticari markalarıdır. Omega oyun içermez ve oyun
dağıtmaz.
