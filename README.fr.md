<p align="center">
  <picture><source media="(prefers-color-scheme: dark)" srcset="docs/img/omega-mark.svg"><img src="docs/img/omega-mark-light.svg" width="96" alt="Omega"></picture>
</p>

<h1 align="center">Omega</h1>

<p align="center">
  Le tableau de bord open source pour les PS5 compatibles homebrew : jeux, Store, musique, amis et party au même endroit.<br>
  Développé par <a href="https://outlinedigital.it"><b>TheCriicom</b></a>.
</p>

<p align="center"><sub><a href="README.md">English</a> · <a href="README.it.md">Italiano</a> · <a href="README.ja.md">日本語</a> · <b>Français</b> · <a href="README.es.md">Español</a> · <a href="README.de.md">Deutsch</a> · <a href="README.nl.md">Nederlands</a> · <a href="README.pt-PT.md">Português (Portugal)</a> · <a href="README.pt-BR.md">Português (Brasil)</a> · <a href="README.ru.md">Русский</a> · <a href="README.ko.md">한국어</a> · <a href="README.zh-Hans.md">简体中文</a> · <a href="README.zh-Hant.md">繁體中文</a> · <a href="README.fi.md">Suomi</a> · <a href="README.sv.md">Svenska</a> · <a href="README.da.md">Dansk</a> · <a href="README.nb.md">Norsk bokmål</a> · <a href="README.pl.md">Polski</a> · <a href="README.tr.md">Türkçe</a> · <a href="README.cs.md">Čeština</a> · <a href="README.hu.md">Magyar</a> · <a href="README.el.md">Ελληνικά</a> · <a href="README.ro.md">Română</a> · <a href="README.th.md">ไทย</a> · <a href="README.vi.md">Tiếng Việt</a> · <a href="README.id.md">Bahasa Indonesia</a> · <a href="README.uk.md">Українська</a></sub></p>

<p align="center">
  <a href="https://play.omegasuite.it">Site web</a> ·
  <a href="https://play.omegasuite.it/installa">Installer</a> ·
  <a href="server/README.md">Héberger un serveur</a> ·
  <a href="client/README.md">Développement</a>
</p>

![Home d'Omega](docs/screenshots/home.png)

Omega est un tableau de bord pensé pour les utilisateurs de homebrew, et il
devient votre Home si vous le souhaitez (il le demande au premier lancement) :
jeux installés et homebrew sont sur la même rangée, le Store installe en un seul
bouton, la musique continue pendant que vous jouez et vos amis sont toujours à
portée de main. Il fonctionne comme un homebrew (SDL2, rendu logiciel) et
communique avec un serveur que chacun peut héberger.

## Fonctionnalités

- **Home** réunissant jeux et homebrew, fonds dynamiques tirés des
  illustrations, lancement direct (jeux via LncUtil, homebrew via websrv,
  payloads ELF en arrière-plan).
- **Store** de homebrew open source : recherche, rayons par catégorie, votes,
  notes et commentaires. Il reconnaît seul les fichiers `.pkg`, `.zip` et `.elf`
  et installe chacun au bon endroit.
- **Ma bibliothèque** : vos propres sauvegardes de jeux, avec un lien de
  téléchargement et une jaquette, ajoutées depuis la console ou depuis votre
  téléphone, ou importées depuis un fichier JSON (une seule fois, ou liées pour
  rester synchronisées). Stockée uniquement sur la console.
- **Musique**, y compris pendant les parties : radio internet (radio-browser),
  Navidrome et autres serveurs Subsonic, fichiers USB, n'importe quel lien
  audio. La lecture tourne dans le daemon en arrière-plan (build FFmpeg
  audio seul), elle ne s'arrête donc pas quand vous lancez un jeu.
- **Télécommande sur téléphone** : le daemon sert une page web sur le port 9095.
  Scannez le QR code affiché sur la console, saisissez le PIN, puis contrôlez la
  musique, envoyez des fichiers audio et gérez Ma bibliothèque depuis le
  navigateur de n'importe quel téléphone, tablette ou PC.
- **Détection du HEN** : au démarrage, Omega reconnaît OnionHEN, etaHEN, pldmgr
  ou ps5_autoloader, vous indique ce qui manque et, après votre confirmation,
  installe et active ses services au bon endroit. Sur OnionHEN, il ajoute au
  menu en jeu (L2 + R3) une page avec les commandes de la musique.
- **Outils système** : températures, seuil du ventilateur, stockage et
  gestionnaire de fichiers.
- **Community** : un mur avec publications, j'aime et commentaires ; discussions
  de groupe ; personnes que vous pourriez connaître ; temps de jeu et classement
  entre amis.
- **Party** avec chat et voix (Opus), invitations à jouer, statut personnalisé
  (en ligne, absent, ne pas déranger, invisible).
- **Game Base** : amis, demandes et messages.
- **Confidentialité** : blocage, signalement, choix de qui peut vous écrire,
  export des données, suppression du compte.
- **Thèmes**, musique d'ambiance générée, navigateur de lecture.
- **27 langues** : l'application suit automatiquement la langue de la console
  (modifiable dans les Paramètres).
- **Choix du serveur** : ajoutez l'adresse de n'importe quel serveur Omega dans
  les Paramètres ; le serveur officiel reste toujours disponible.

| | |
|---|---|
| ![Store](docs/screenshots/store.png) | ![Community](docs/screenshots/community.png) |
| ![Party](docs/screenshots/party.png) | ![Temps de jeu](docs/screenshots/stats.png) |
| ![Télécommande sur téléphone depuis un PC](docs/screenshots/remote-music.png) | ![Ma bibliothèque depuis un PC](docs/screenshots/remote-library.png) |

## Organisation du dépôt

| Dossier | |
|---|---|
| [`client/`](client) | l'application console (C, SDL2) et une version de bureau pour la développer sur Mac |
| [`daemon/`](daemon) | payload en arrière-plan : lecteur de musique, page web de la télécommande, Ma bibliothèque, notifications pendant les parties et (uniquement si vous le choisissez) réouverture d'Omega quand vous revenez à la Home |
| [`onionhen-plugin/`](onionhen-plugin) | plugin OnionHEN : une page Omega dans le menu en jeu avec les commandes de la musique |
| [`server/`](server) | API Node.js, proxy, panneau de modération et Docker Compose pour héberger un serveur |
| [`docs/`](docs) | architecture et images |

## Installer Omega sur la console

Il vous faut une PS5 jailbreakée prenant en charge les homebrew : un HEN
comme OnionHEN ou etaHEN, ou un lanceur comme [websrv](https://github.com/ps5-payload-dev/websrv)
avec un chargeur de payloads. Le plus simple est d'ouvrir
[play.omegasuite.it/installa](https://play.omegasuite.it/installa) dans le
navigateur de la console. Sinon, téléchargez le paquet depuis le site web et
copiez `data/` sur la console par FTP.

## Héberger un serveur

```sh
git clone https://github.com/CristianLaporta/omega-dashboard-ps5.git
cd omega-dashboard-ps5/server
./scripts/install.sh
```

Le script crée le fichier `.env` avec des secrets aléatoires et démarre les
conteneurs. Le guide complet, avec HTTPS automatique via Caddy, se trouve dans
[`server/README.md`](server/README.md). Ensuite, sur la console :
**Paramètres → Serveur → Ajouter un serveur**.

## Développement

- Application : [`client/README.md`](client/README.md) — version PS5 avec
  [ps5-payload-sdk](https://github.com/ps5-payload-dev/sdk) et version de bureau
  avec la SDL2 de Homebrew, pilotée par un fichier de commandes pour tester
  l'interface sans console. Les traductions se trouvent dans `client/i18n/`
  (un JSON par langue).
- Serveur : [`server/README.md`](server/README.md) — Node.js ≥ 20 et PostgreSQL,
  tests de bout en bout avec `npm test`.
- Architecture : [`docs/architecture.md`](docs/architecture.md).

Les contributions sont les bienvenues : consultez [CONTRIBUTING.md](CONTRIBUTING.md).

## Auteur

Omega est développé par **TheCriicom** — [outlinedigital.it](https://outlinedigital.it).

## Licence

Copyright © 2026 TheCriicom et les contributeurs d'Omega.
Omega est un logiciel libre : [GNU GPL v3 ou ultérieure](LICENSE). Les
composants tiers et leurs licences sont listés dans
[`client/THIRD-PARTY-NOTICES.md`](client/THIRD-PARTY-NOTICES.md).

Omega est un projet indépendant ; il n'est ni affilié à Sony Interactive
Entertainment, ni approuvé ou sponsorisé par celle-ci. « PlayStation » et
« PS5 » sont des marques de leurs propriétaires respectifs. Omega ne contient
ni ne distribue aucun jeu.
