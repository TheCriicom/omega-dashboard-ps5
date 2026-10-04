<p align="center">
  <picture><source media="(prefers-color-scheme: dark)" srcset="docs/img/omega-mark.svg"><img src="docs/img/omega-mark-light.svg" width="96" alt="Omega"></picture>
</p>

<h1 align="center">Omega</h1>

<p align="center">
  El panel de código abierto para consolas PS5 con homebrew: juegos, Store, música, amigos y party en un solo lugar.<br>
  Desarrollado por <a href="https://outlinedigital.it"><b>TheCriicom</b></a>.
</p>

<p align="center"><sub><a href="README.md">English</a> · <a href="README.it.md">Italiano</a> · <a href="README.ja.md">日本語</a> · <a href="README.fr.md">Français</a> · <b>Español</b> · <a href="README.de.md">Deutsch</a> · <a href="README.nl.md">Nederlands</a> · <a href="README.pt-PT.md">Português (Portugal)</a> · <a href="README.pt-BR.md">Português (Brasil)</a> · <a href="README.ru.md">Русский</a> · <a href="README.ko.md">한국어</a> · <a href="README.zh-Hans.md">简体中文</a> · <a href="README.zh-Hant.md">繁體中文</a> · <a href="README.fi.md">Suomi</a> · <a href="README.sv.md">Svenska</a> · <a href="README.da.md">Dansk</a> · <a href="README.nb.md">Norsk bokmål</a> · <a href="README.pl.md">Polski</a> · <a href="README.tr.md">Türkçe</a> · <a href="README.cs.md">Čeština</a> · <a href="README.hu.md">Magyar</a> · <a href="README.el.md">Ελληνικά</a> · <a href="README.ro.md">Română</a> · <a href="README.th.md">ไทย</a> · <a href="README.vi.md">Tiếng Việt</a> · <a href="README.id.md">Bahasa Indonesia</a> · <a href="README.uk.md">Українська</a></sub></p>

<p align="center">
  <a href="https://play.omegasuite.it">Sitio web</a> ·
  <a href="https://play.omegasuite.it/installa">Instalar</a> ·
  <a href="server/README.md">Alojar un servidor</a> ·
  <a href="client/README.md">Desarrollo</a>
</p>

![Home de Omega](docs/screenshots/home.png)

Omega es un panel pensado para quienes usan homebrew, y puede ser tu Home si
quieres (te lo pregunta en el primer arranque): los juegos instalados y los
homebrew comparten la misma fila, el Store instala con un solo botón, la música
sigue sonando mientras juegas y tus amigos están siempre a un toque. Funciona
como homebrew (SDL2, renderizado por software) y se comunica con un servidor que
cualquiera puede alojar.

## Qué incluye

- **Home** con juegos y homebrew juntos, fondos dinámicos a partir de las
  ilustraciones, inicio directo (juegos mediante LncUtil, homebrew mediante
  websrv, payloads ELF en segundo plano).
- **Store** de homebrew de código abierto: búsqueda, estanterías por categoría,
  votos, valoraciones y comentarios. Detecta por sí solo `.pkg`, `.zip` y `.elf`
  e instala cada uno en el lugar adecuado.
- **Mi biblioteca**: tus propias copias de seguridad de juegos, con enlace de
  descarga y carátula, añadidas desde la consola o desde el móvil, o importadas
  desde un archivo JSON (una sola vez, o vinculado para que se mantenga
  sincronizado). Se guarda solo en la consola.
- **Música**, también durante las partidas: radio por internet (radio-browser),
  Navidrome y otros servidores Subsonic, archivos USB, cualquier enlace de
  audio. La reproducción corre en el daemon en segundo plano (compilación de
  FFmpeg solo de audio), así que no se detiene al abrir un juego.
- **Mando en el móvil**: el daemon sirve una página web en el puerto 9095.
  Escanea el código QR que aparece en la consola, escribe el PIN y controla la
  música, envía archivos de audio y gestiona Mi biblioteca desde el navegador de
  cualquier móvil, tableta u ordenador.
- **Detección de HEN**: al arrancar, Omega reconoce OnionHEN, etaHEN, pldmgr o
  ps5_autoloader, te dice qué falta y, tras tu confirmación, instala y activa sus
  servicios en el lugar adecuado. En OnionHEN añade al menú del juego (L2 + R3)
  una página con los controles de la música.
- **Herramientas del sistema**: temperaturas, umbral del ventilador,
  almacenamiento y un gestor de archivos.
- **Community**: un muro con publicaciones, «me gusta» y comentarios; chats de
  grupo; personas que quizá conozcas; tiempo de juego y clasificación de amigos.
- **Party** con chat y voz (Opus), invitaciones a partidas y estado
  personalizado (en línea, ausente, no molestar, invisible).
- **Game Base**: amigos, solicitudes y mensajes.
- **Privacidad**: bloqueos, denuncias, quién puede escribirte, exportación de
  datos y eliminación de la cuenta.
- **Temas**, música ambiental generada y un navegador de lectura.
- **27 idiomas**: la app sigue automáticamente el idioma de la consola (se puede
  cambiar en Ajustes).
- **Elige tu servidor**: añade en Ajustes la dirección de cualquier servidor
  Omega; el oficial siempre sigue disponible.

| | |
|---|---|
| ![Store](docs/screenshots/store.png) | ![Community](docs/screenshots/community.png) |
| ![Party](docs/screenshots/party.png) | ![Tiempo de juego](docs/screenshots/stats.png) |
| ![Mando en el móvil desde un ordenador](docs/screenshots/remote-music.png) | ![Mi biblioteca desde un ordenador](docs/screenshots/remote-library.png) |

## Estructura del repositorio

| Carpeta | |
|---|---|
| [`client/`](client) | la app de la consola (C, SDL2) y una versión de escritorio para desarrollarla en un Mac |
| [`daemon/`](daemon) | payload en segundo plano: reproductor de música, página web del mando, Mi biblioteca, notificaciones durante las partidas y (solo si lo eliges) reabrir Omega al regresar a la Home |
| [`onionhen-plugin/`](onionhen-plugin) | plugin de OnionHEN: una página de Omega en el menú del juego con controles de música |
| [`server/`](server) | API en Node.js, proxy, panel de moderación y Docker Compose para alojar un servidor |
| [`docs/`](docs) | arquitectura e imágenes |

## Instalar Omega en la consola

Necesitas una PS5 con jailbreak y soporte para homebrew: un HEN como OnionHEN o
etaHEN, o un lanzador como [websrv](https://github.com/ps5-payload-dev/websrv)
con un cargador de payloads.
La forma más sencilla es abrir
[play.omegasuite.it/installa](https://play.omegasuite.it/installa) en el
navegador de la consola. Otra opción es descargar el paquete desde el sitio web
y copiar `data/` a la consola por FTP.

## Alojar un servidor

```sh
git clone https://github.com/TheCriicom/omega-dashboard-ps5.git
cd omega-dashboard-ps5/server
./scripts/install.sh
```

El script crea el archivo `.env` con secretos aleatorios e inicia los
contenedores. La guía completa, con HTTPS automático mediante Caddy, está en
[`server/README.md`](server/README.md). Después, en la consola:
**Ajustes → Servidor → Añadir un servidor**.

## Desarrollo

- App: [`client/README.md`](client/README.md) — versión para PS5 con
  [ps5-payload-sdk](https://github.com/ps5-payload-dev/sdk) y versión de
  escritorio con la SDL2 de Homebrew, controlada mediante un archivo de comandos
  para probar la interfaz sin consola. Las traducciones están en `client/i18n/`
  (un JSON por idioma).
- Servidor: [`server/README.md`](server/README.md) — Node.js ≥ 20 y PostgreSQL,
  pruebas de extremo a extremo con `npm test`.
- Arquitectura: [`docs/architecture.md`](docs/architecture.md).

Las contribuciones son bienvenidas: consulta [CONTRIBUTING.md](CONTRIBUTING.md).

## Autor

Omega está desarrollado por **TheCriicom** — [outlinedigital.it](https://outlinedigital.it).

## Licencia

Copyright © 2026 TheCriicom y los colaboradores de Omega.
Omega es software libre: [GNU GPL v3 o posterior](LICENSE). Los componentes de
terceros y sus licencias se indican en
[`client/THIRD-PARTY-NOTICES.md`](client/THIRD-PARTY-NOTICES.md).

Omega es un proyecto independiente y no está afiliado a Sony Interactive
Entertainment, ni cuenta con su aprobación o patrocinio. «PlayStation» y «PS5»
son marcas comerciales de sus respectivos propietarios. Omega no contiene ni
distribuye juegos.
