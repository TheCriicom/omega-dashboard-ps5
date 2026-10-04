<p align="center">
  <picture><source media="(prefers-color-scheme: dark)" srcset="docs/img/omega-mark.svg"><img src="docs/img/omega-mark-light.svg" width="96" alt="Omega"></picture>
</p>

<h1 align="center">Omega</h1>

<p align="center">
  Ο ανοιχτού κώδικα πίνακας ελέγχου για κονσόλες PS5 με homebrew: παιχνίδια, Store, μουσική, φίλοι και party σε ένα μέρος.<br>
  Ανάπτυξη από <a href="https://outlinedigital.it"><b>TheCriicom</b></a>.
</p>

<p align="center"><sub><a href="README.md">English</a> · <a href="README.it.md">Italiano</a> · <a href="README.ja.md">日本語</a> · <a href="README.fr.md">Français</a> · <a href="README.es.md">Español</a> · <a href="README.de.md">Deutsch</a> · <a href="README.nl.md">Nederlands</a> · <a href="README.pt-PT.md">Português (Portugal)</a> · <a href="README.pt-BR.md">Português (Brasil)</a> · <a href="README.ru.md">Русский</a> · <a href="README.ko.md">한국어</a> · <a href="README.zh-Hans.md">简体中文</a> · <a href="README.zh-Hant.md">繁體中文</a> · <a href="README.fi.md">Suomi</a> · <a href="README.sv.md">Svenska</a> · <a href="README.da.md">Dansk</a> · <a href="README.nb.md">Norsk bokmål</a> · <a href="README.pl.md">Polski</a> · <a href="README.tr.md">Türkçe</a> · <a href="README.cs.md">Čeština</a> · <a href="README.hu.md">Magyar</a> · <b>Ελληνικά</b> · <a href="README.ro.md">Română</a> · <a href="README.th.md">ไทย</a> · <a href="README.vi.md">Tiếng Việt</a> · <a href="README.id.md">Bahasa Indonesia</a> · <a href="README.uk.md">Українська</a></sub></p>

<p align="center">
  <a href="https://play.omegasuite.it">Ιστότοπος</a> ·
  <a href="https://play.omegasuite.it/installa">Εγκατάσταση</a> ·
  <a href="server/README.md">Φιλοξενία διακομιστή</a> ·
  <a href="client/README.md">Ανάπτυξη</a>
</p>

![Η Home του Omega](docs/screenshots/home.png)

Το Omega είναι ένας πίνακας ελέγχου φτιαγμένος για χρήστες homebrew και, αν το
θέλετε, η Home σας (ρωτάει στην πρώτη εκκίνηση): τα εγκατεστημένα παιχνίδια και
τα homebrew βρίσκονται στην ίδια σειρά, το Store εγκαθιστά με ένα κουμπί, η
μουσική συνεχίζει να παίζει όσο παίζετε και οι φίλοι σας είναι πάντα ένα πάτημα
μακριά. Εκτελείται ως homebrew (SDL2, απόδοση μέσω λογισμικού) και επικοινωνεί
με έναν διακομιστή που μπορεί να φιλοξενήσει οποιοσδήποτε.

## Τι περιλαμβάνει

- **Home** με παιχνίδια και homebrew μαζί, δυναμικά φόντα από τα εικαστικά και
  άμεση εκκίνηση (παιχνίδια μέσω LncUtil, homebrew μέσω websrv, ELF payloads στο
  παρασκήνιο).
- **Store** με homebrew ανοιχτού κώδικα: αναζήτηση, ράφια ανά κατηγορία, ψήφοι,
  βαθμολογίες και σχόλια. Αναγνωρίζει μόνο του αρχεία `.pkg`, `.zip` και `.elf`
  και εγκαθιστά το καθένα στη σωστή θέση.
- **Η βιβλιοθήκη μου**: αντίγραφα ασφαλείας των δικών σας παιχνιδιών με σύνδεσμο
  λήψης και εξώφυλλο, που προστίθενται από την κονσόλα ή από το κινητό ή
  εισάγονται από αρχείο JSON (μία φορά ή συνδεδεμένα, ώστε να μένουν
  συγχρονισμένα). Αποθηκεύεται μόνο στην κονσόλα.
- **Μουσική**, και κατά τη διάρκεια των παιχνιδιών: διαδικτυακό ραδιόφωνο
  (radio-browser), Navidrome και άλλοι διακομιστές Subsonic, αρχεία USB,
  οποιοσδήποτε σύνδεσμος ήχου. Η αναπαραγωγή τρέχει στο daemon στο παρασκήνιο
  (έκδοση FFmpeg μόνο για ήχο), οπότε δεν σταματά όταν ανοίγετε ένα παιχνίδι.
- **Τηλεχειριστήριο κινητού**: το daemon σερβίρει μια ιστοσελίδα στη θύρα 9095.
  Σαρώστε τον κωδικό QR που εμφανίζεται στην κονσόλα, πληκτρολογήστε το PIN και
  ελέγξτε τη μουσική, στείλτε αρχεία ήχου και διαχειριστείτε τη Βιβλιοθήκη μου
  από πρόγραμμα περιήγησης σε οποιοδήποτε κινητό, tablet ή υπολογιστή.
- **Εντοπισμός HEN**: κατά την εκκίνηση το Omega αναγνωρίζει τα OnionHEN,
  etaHEN, pldmgr ή ps5_autoloader, σας λέει τι λείπει και, αφού επιβεβαιώσετε,
  εγκαθιστά και ενεργοποιεί τις υπηρεσίες τους στη σωστή θέση. Στο OnionHEN
  προσθέτει στο μενού εντός παιχνιδιού (L2 + R3) μια σελίδα με χειριστήρια
  μουσικής.
- **Εργαλεία συστήματος**: θερμοκρασίες, όριο ανεμιστήρα, αποθήκευση και
  διαχείριση αρχείων.
- **Community**: τοίχος με αναρτήσεις, «μου αρέσει» και σχόλια· ομαδικές
  συνομιλίες· άτομα που ίσως γνωρίζετε· χρόνος παιχνιδιού και κατάταξη φίλων.
- **Party** με συνομιλία και φωνή (Opus), προσκλήσεις σε παιχνίδι και
  προσαρμοσμένη κατάσταση (συνδεδεμένος, εκτός, μην ενοχλείτε, αόρατος).
- **Game Base**: φίλοι, αιτήματα και μηνύματα.
- **Απόρρητο**: αποκλεισμός, αναφορά, ποιος μπορεί να σας στέλνει μηνύματα,
  εξαγωγή δεδομένων, διαγραφή λογαριασμού.
- **Θέματα**, παραγόμενη μουσική ατμόσφαιρας, πρόγραμμα περιήγησης για ανάγνωση.
- **27 γλώσσες**: η εφαρμογή ακολουθεί αυτόματα τη γλώσσα της κονσόλας (μπορεί
  να αλλάξει από τις Ρυθμίσεις).
- **Επιλογή διακομιστή**: από τις Ρυθμίσεις μπορείτε να προσθέσετε τη διεύθυνση
  οποιουδήποτε διακομιστή Omega· ο επίσημος παραμένει πάντα διαθέσιμος.

| | |
|---|---|
| ![Store](docs/screenshots/store.png) | ![Community](docs/screenshots/community.png) |
| ![Party](docs/screenshots/party.png) | ![Χρόνος παιχνιδιού](docs/screenshots/stats.png) |
| ![Τηλεχειριστήριο κινητού σε υπολογιστή](docs/screenshots/remote-music.png) | ![Η βιβλιοθήκη μου από υπολογιστή](docs/screenshots/remote-library.png) |

## Δομή αποθετηρίου

| Φάκελος | |
|---|---|
| [`client/`](client) | η εφαρμογή της κονσόλας (C, SDL2) και μια έκδοση για υπολογιστή για ανάπτυξη σε Mac |
| [`daemon/`](daemon) | payload παρασκηνίου: αναπαραγωγή μουσικής, ιστοσελίδα τηλεχειριστηρίου κινητού, Η βιβλιοθήκη μου, ειδοποιήσεις κατά τη διάρκεια του παιχνιδιού και (μόνο αν το επιλέξετε) ξανάνοιγμα του Omega όταν επιστρέφετε στην Home |
| [`onionhen-plugin/`](onionhen-plugin) | plugin για OnionHEN: μια σελίδα του Omega στο μενού εντός παιχνιδιού με χειριστήρια μουσικής |
| [`server/`](server) | API σε Node.js, proxy, πίνακας εποπτείας και Docker Compose για τη φιλοξενία διακομιστή |
| [`docs/`](docs) | αρχιτεκτονική και εικόνες |

## Εγκατάσταση του Omega στην κονσόλα

Χρειάζεστε PS5 με jailbreak και υποστήριξη homebrew: ένα HEN όπως το OnionHEN ή
το etaHEN ή έναν launcher όπως το [websrv](https://github.com/ps5-payload-dev/websrv) με έναν payload loader.
Ο ευκολότερος τρόπος είναι να ανοίξετε τη σελίδα
[play.omegasuite.it/installa](https://play.omegasuite.it/installa) στο πρόγραμμα
περιήγησης της κονσόλας. Εναλλακτικά, κατεβάστε το πακέτο από τον ιστότοπο και
αντιγράψτε το `data/` στην κονσόλα μέσω FTP.

## Φιλοξενία διακομιστή

```sh
git clone https://github.com/CristianLaporta/omega-dashboard-ps5.git
cd omega-dashboard-ps5/server
./scripts/install.sh
```

Το script δημιουργεί το `.env` με τυχαία μυστικά κλειδιά και ξεκινά τα
containers. Ο πλήρης οδηγός, με αυτόματο HTTPS μέσω Caddy, βρίσκεται στο
[`server/README.md`](server/README.md). Έπειτα, στην κονσόλα:
**Ρυθμίσεις → Διακομιστής → Προσθήκη διακομιστή**.

## Ανάπτυξη

- Εφαρμογή: [`client/README.md`](client/README.md) — έκδοση για PS5 με το
  [ps5-payload-sdk](https://github.com/ps5-payload-dev/sdk) και έκδοση για
  υπολογιστή με το SDL2 του Homebrew, που ελέγχεται από αρχείο εντολών ώστε να
  δοκιμάζετε το περιβάλλον χωρίς κονσόλα. Οι μεταφράσεις βρίσκονται στο
  `client/i18n/` (ένα JSON ανά γλώσσα).
- Διακομιστής: [`server/README.md`](server/README.md) — Node.js ≥ 20 και
  PostgreSQL, δοκιμές end-to-end με `npm test`.
- Αρχιτεκτονική: [`docs/architecture.md`](docs/architecture.md).

Οι συνεισφορές είναι ευπρόσδεκτες: δείτε το [CONTRIBUTING.md](CONTRIBUTING.md).

## Δημιουργός

Το Omega αναπτύσσεται από τον **TheCriicom** — [outlinedigital.it](https://outlinedigital.it).

## Άδεια

Copyright © 2026 TheCriicom και οι συντελεστές του Omega.
Το Omega είναι ελεύθερο λογισμικό: [GNU GPL v3 ή νεότερη](LICENSE). Τα στοιχεία
τρίτων και οι άδειές τους παρατίθενται στο
[`client/THIRD-PARTY-NOTICES.md`](client/THIRD-PARTY-NOTICES.md).

Το Omega είναι ανεξάρτητο έργο και δεν συνδέεται με τη Sony Interactive
Entertainment, ούτε έχει εγκριθεί ή χορηγηθεί από αυτήν. Τα «PlayStation» και
«PS5» είναι εμπορικά σήματα των αντίστοιχων κατόχων τους. Το Omega δεν περιέχει
ούτε διανέμει παιχνίδια.
