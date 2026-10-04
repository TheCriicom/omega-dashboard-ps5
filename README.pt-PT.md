<p align="center">
  <picture><source media="(prefers-color-scheme: dark)" srcset="docs/img/omega-mark.svg"><img src="docs/img/omega-mark-light.svg" width="96" alt="Omega"></picture>
</p>

<h1 align="center">Omega</h1>

<p align="center">
  O painel de código aberto para consolas PS5 com homebrew: jogos, Store, música, amigos e party num só lugar.<br>
  Desenvolvido por <a href="https://outlinedigital.it"><b>TheCriicom</b></a>.
</p>

<p align="center"><sub><a href="README.md">English</a> · <a href="README.it.md">Italiano</a> · <a href="README.ja.md">日本語</a> · <a href="README.fr.md">Français</a> · <a href="README.es.md">Español</a> · <a href="README.de.md">Deutsch</a> · <a href="README.nl.md">Nederlands</a> · <b>Português (Portugal)</b> · <a href="README.pt-BR.md">Português (Brasil)</a> · <a href="README.ru.md">Русский</a> · <a href="README.ko.md">한국어</a> · <a href="README.zh-Hans.md">简体中文</a> · <a href="README.zh-Hant.md">繁體中文</a> · <a href="README.fi.md">Suomi</a> · <a href="README.sv.md">Svenska</a> · <a href="README.da.md">Dansk</a> · <a href="README.nb.md">Norsk bokmål</a> · <a href="README.pl.md">Polski</a> · <a href="README.tr.md">Türkçe</a> · <a href="README.cs.md">Čeština</a> · <a href="README.hu.md">Magyar</a> · <a href="README.el.md">Ελληνικά</a> · <a href="README.ro.md">Română</a> · <a href="README.th.md">ไทย</a> · <a href="README.vi.md">Tiếng Việt</a> · <a href="README.id.md">Bahasa Indonesia</a> · <a href="README.uk.md">Українська</a></sub></p>

<p align="center">
  <a href="https://play.omegasuite.it">Site</a> ·
  <a href="https://play.omegasuite.it/installa">Instalar</a> ·
  <a href="server/README.md">Alojar um servidor</a> ·
  <a href="client/README.md">Desenvolvimento</a>
</p>

![Home do Omega](docs/screenshots/home.png)

O Omega é um painel pensado para quem usa homebrew e, se quiseres, o teu ecrã
Home (pergunta no primeiro arranque): os jogos instalados e os homebrew ficam na
mesma fila, a Store instala com um só botão, a música continua a tocar enquanto
jogas e os teus amigos estão sempre à distância de um clique. Corre como
homebrew (SDL2, renderizador por software) e comunica com um servidor que
qualquer pessoa pode alojar.

## O que inclui

- **Home** com jogos e homebrew juntos, fundos dinâmicos a partir da arte,
  arranque direto (jogos através do LncUtil, homebrew através do websrv,
  payloads ELF em segundo plano).
- **Store** de homebrew de código aberto: pesquisa, prateleiras por categoria,
  votos, classificações e comentários. Deteta sozinha `.pkg`, `.zip` e `.elf` e
  instala cada um no sítio certo.
- **A minha biblioteca**: as cópias de segurança dos teus jogos, com ligação de
  transferência e uma capa, adicionadas a partir da consola ou do telemóvel, ou
  importadas de um ficheiro JSON (uma vez, ou ligado para ficar sincronizado).
  Guardada apenas na consola.
- **Música**, também durante os jogos: rádio na internet (radio-browser),
  Navidrome e outros servidores Subsonic, ficheiros USB, qualquer ligação de
  áudio. A reprodução corre no daemon em segundo plano (build do FFmpeg só de
  áudio), por isso não pára quando abres um jogo.
- **Comando no telemóvel**: o daemon serve uma página web na porta 9095. Lê o
  código QR mostrado na consola, escreve o PIN e controla a música, envia
  ficheiros de áudio e gere A minha biblioteca a partir do navegador de
  qualquer telemóvel, tablet ou PC.
- **Deteção do HEN**: no arranque o Omega reconhece o OnionHEN, o etaHEN, o
  pldmgr ou o ps5_autoloader, diz-te o que falta e, depois de confirmares,
  instala e ativa os respetivos serviços no sítio certo. No OnionHEN acrescenta
  uma página ao menu durante o jogo (L2 + R3) com os controlos da música.
- **Ferramentas do sistema**: temperaturas, limiar da ventoinha, armazenamento e
  um gestor de ficheiros.
- **Community**: um mural com publicações, gostos e comentários; chats de grupo;
  pessoas que talvez conheças; tempo de jogo e uma classificação de amigos.
- **Party** com chat e voz (Opus), convites para jogar, estado personalizado
  (online, ausente, não incomodar, invisível).
- **Game Base**: amigos, pedidos e mensagens.
- **Privacidade**: bloqueios, denúncias, quem te pode enviar mensagens,
  exportação de dados, eliminação da conta.
- **Temas**, música ambiente gerada, um navegador de leitura.
- **27 idiomas**: a aplicação segue automaticamente o idioma da consola (e
  pode ser alterado nas Definições).
- **Escolhe o teu servidor**: adiciona nas Definições o endereço de qualquer
  servidor Omega; o oficial continua sempre disponível.

| | |
|---|---|
| ![Store](docs/screenshots/store.png) | ![Community](docs/screenshots/community.png) |
| ![Party](docs/screenshots/party.png) | ![Tempo de jogo](docs/screenshots/stats.png) |
| ![Comando no telemóvel num PC](docs/screenshots/remote-music.png) | ![A minha biblioteca a partir de um PC](docs/screenshots/remote-library.png) |

## Estrutura do repositório

| Pasta | |
|---|---|
| [`client/`](client) | a aplicação para a consola (C, SDL2) e uma build para computador para a desenvolver num Mac |
| [`daemon/`](daemon) | payload em segundo plano: leitor de música, página web do comando no telemóvel, A minha biblioteca, notificações durante os jogos e (só se o escolheres) reabrir o Omega quando voltas à Home |
| [`onionhen-plugin/`](onionhen-plugin) | plugin do OnionHEN: uma página do Omega no menu durante o jogo com controlos da música |
| [`server/`](server) | API em Node.js, proxy, painel de moderação e Docker Compose para alojar um servidor |
| [`docs/`](docs) | arquitetura e imagens |

## Instalar o Omega na consola

Precisas de uma PS5 com jailbreak e suporte para homebrew: um HEN como o
OnionHEN ou o etaHEN, ou um launcher como o
[websrv](https://github.com/ps5-payload-dev/websrv) com um carregador de payloads.
A forma mais simples é abrir [play.omegasuite.it/installa](https://play.omegasuite.it/installa)
no navegador da consola. Em alternativa, transfere o pacote a partir do site e
copia `data/` para a consola por FTP.

## Alojar um servidor

```sh
git clone https://github.com/TheCriicom/omega-dashboard-ps5.git
cd omega-dashboard-ps5/server
./scripts/install.sh
```

O script cria o `.env` com segredos aleatórios e inicia os contentores. O guia
completo, com HTTPS automático através do Caddy, está em
[`server/README.md`](server/README.md). Depois, na consola:
**Definições → Servidor → Adicionar um servidor**.

## Desenvolvimento

- Aplicação: [`client/README.md`](client/README.md) — build para PS5 com o
  [ps5-payload-sdk](https://github.com/ps5-payload-dev/sdk) e uma build para
  computador com o SDL2 do Homebrew, controlada por um ficheiro de comandos para
  testar a interface sem consola. As traduções estão em `client/i18n/` (um JSON
  por idioma).
- Servidor: [`server/README.md`](server/README.md) — Node.js ≥ 20 e PostgreSQL,
  testes end-to-end com `npm test`.
- Arquitetura: [`docs/architecture.md`](docs/architecture.md).

As contribuições são bem-vindas: consulta o [CONTRIBUTING.md](CONTRIBUTING.md).

## Autor

O Omega é desenvolvido por **TheCriicom** — [outlinedigital.it](https://outlinedigital.it).

## Licença

Copyright © 2026 TheCriicom e os contribuidores do Omega.
O Omega é software livre: [GNU GPL v3 ou posterior](LICENSE). Os componentes de
terceiros e as respetivas licenças estão listados em
[`client/THIRD-PARTY-NOTICES.md`](client/THIRD-PARTY-NOTICES.md).

O Omega é um projeto independente e não é afiliado, aprovado nem patrocinado
pela Sony Interactive Entertainment. "PlayStation" e "PS5" são marcas
registadas dos respetivos proprietários. O Omega não contém nem distribui
jogos.
