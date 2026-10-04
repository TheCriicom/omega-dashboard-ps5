<p align="center">
  <picture><source media="(prefers-color-scheme: dark)" srcset="docs/img/omega-mark.svg"><img src="docs/img/omega-mark-light.svg" width="96" alt="Omega"></picture>
</p>

<h1 align="center">Omega</h1>

<p align="center">
  O painel de código aberto para consoles PS5 com homebrew: jogos, Store, música, amigos e party em um só lugar.<br>
  Desenvolvido por <a href="https://outlinedigital.it"><b>TheCriicom</b></a>.
</p>

<p align="center"><sub><a href="README.md">English</a> · <a href="README.it.md">Italiano</a> · <a href="README.ja.md">日本語</a> · <a href="README.fr.md">Français</a> · <a href="README.es.md">Español</a> · <a href="README.de.md">Deutsch</a> · <a href="README.nl.md">Nederlands</a> · <a href="README.pt-PT.md">Português (Portugal)</a> · <b>Português (Brasil)</b> · <a href="README.ru.md">Русский</a> · <a href="README.ko.md">한국어</a> · <a href="README.zh-Hans.md">简体中文</a> · <a href="README.zh-Hant.md">繁體中文</a> · <a href="README.fi.md">Suomi</a> · <a href="README.sv.md">Svenska</a> · <a href="README.da.md">Dansk</a> · <a href="README.nb.md">Norsk bokmål</a> · <a href="README.pl.md">Polski</a> · <a href="README.tr.md">Türkçe</a> · <a href="README.cs.md">Čeština</a> · <a href="README.hu.md">Magyar</a> · <a href="README.el.md">Ελληνικά</a> · <a href="README.ro.md">Română</a> · <a href="README.th.md">ไทย</a> · <a href="README.vi.md">Tiếng Việt</a> · <a href="README.id.md">Bahasa Indonesia</a> · <a href="README.uk.md">Українська</a></sub></p>

<p align="center">
  <a href="https://play.omegasuite.it">Site</a> ·
  <a href="https://play.omegasuite.it/installa">Instalar</a> ·
  <a href="server/README.md">Hospedar um servidor</a> ·
  <a href="client/README.md">Desenvolvimento</a>
</p>

![Home do Omega](docs/screenshots/home.png)

O Omega é um painel feito para quem usa homebrew e, se você quiser, a sua tela
Home (ele pergunta na primeira abertura): os jogos instalados e os homebrews
ficam na mesma fileira, a Store instala com um só botão, a música continua
tocando enquanto você joga e seus amigos estão sempre a um toque de distância.
Ele roda como homebrew (SDL2, renderizador por software) e se comunica com um
servidor que qualquer pessoa pode hospedar.

## O que tem dentro

- **Home** com jogos e homebrews juntos, planos de fundo dinâmicos a partir da
  arte, inicialização direta (jogos pelo LncUtil, homebrews pelo websrv,
  payloads ELF em segundo plano).
- **Store** de homebrews de código aberto: busca, prateleiras por categoria,
  votos, avaliações e comentários. Detecta sozinha `.pkg`, `.zip` e `.elf` e
  instala cada um no lugar certo.
- **Minha biblioteca**: os backups dos seus próprios jogos, com link de download
  e uma capa, adicionados pelo console ou pelo celular, ou importados de um
  arquivo JSON (uma vez, ou vinculado para ficar sincronizado). Guardada apenas
  no console.
- **Música**, também durante os jogos: rádio pela internet (radio-browser),
  Navidrome e outros servidores Subsonic, arquivos USB, qualquer link de áudio.
  A reprodução roda no daemon em segundo plano (build do FFmpeg só de áudio),
  então não para quando você abre um jogo.
- **Controle pelo celular**: o daemon serve uma página web na porta 9095. Leia o
  QR code mostrado no console, digite o PIN e controle a música, envie arquivos
  de áudio e gerencie a Minha biblioteca pelo navegador de qualquer celular,
  tablet ou PC.
- **Detecção do HEN**: na inicialização o Omega reconhece OnionHEN, etaHEN,
  pldmgr ou ps5_autoloader, avisa o que está faltando e, depois que você
  confirma, instala e ativa os serviços no lugar certo. No OnionHEN ele adiciona
  uma página ao menu durante o jogo (L2 + R3) com controles da música.
- **Ferramentas do sistema**: temperaturas, limite do cooler, armazenamento e um
  gerenciador de arquivos.
- **Community**: um mural com posts, curtidas e comentários; chats em grupo;
  pessoas que você talvez conheça; tempo de jogo e um ranking entre amigos.
- **Party** com chat e voz (Opus), convites para jogar, status personalizado
  (online, ausente, não perturbe, invisível).
- **Game Base**: amigos, solicitações e mensagens.
- **Privacidade**: bloqueios, denúncias, quem pode enviar mensagens para você,
  exportação de dados, exclusão da conta.
- **Temas**, música ambiente gerada, um navegador de leitura.
- **27 idiomas**: o app segue automaticamente o idioma do console (e dá para
  mudar nas Configurações).
- **Escolha seu servidor**: adicione nas Configurações o endereço de qualquer
  servidor Omega; o oficial continua sempre disponível.

| | |
|---|---|
| ![Store](docs/screenshots/store.png) | ![Community](docs/screenshots/community.png) |
| ![Party](docs/screenshots/party.png) | ![Tempo de jogo](docs/screenshots/stats.png) |
| ![Controle pelo celular em um PC](docs/screenshots/remote-music.png) | ![Minha biblioteca a partir de um PC](docs/screenshots/remote-library.png) |

## Estrutura do repositório

| Pasta | |
|---|---|
| [`client/`](client) | o app do console (C, SDL2) e uma build para desktop para desenvolvê-lo no Mac |
| [`daemon/`](daemon) | payload em segundo plano: player de música, página web do controle pelo celular, Minha biblioteca, notificações durante os jogos e (só se você escolher) reabrir o Omega quando você volta para a Home |
| [`onionhen-plugin/`](onionhen-plugin) | plugin do OnionHEN: uma página do Omega no menu durante o jogo com controles da música |
| [`server/`](server) | API em Node.js, proxy, painel de moderação e Docker Compose para hospedar um servidor |
| [`docs/`](docs) | arquitetura e imagens |

## Instalando o Omega no console

Você precisa de um PS5 desbloqueado (jailbreak) com suporte a homebrew: um HEN
como o OnionHEN ou o etaHEN, ou um launcher como o
[websrv](https://github.com/ps5-payload-dev/websrv) com um carregador de
payloads. O jeito mais fácil é abrir
[play.omegasuite.it/installa](https://play.omegasuite.it/installa) no navegador
do console. Como alternativa, baixe o pacote no site e copie `data/` para o
console via FTP.

## Hospedando um servidor

```sh
git clone https://github.com/TheCriicom/omega-dashboard-ps5.git
cd omega-dashboard-ps5/server
./scripts/install.sh
```

O script cria o `.env` com segredos aleatórios e inicia os contêineres. O guia
completo, com HTTPS automático pelo Caddy, está em
[`server/README.md`](server/README.md). Depois, no console:
**Configurações → Servidor → Adicionar um servidor**.

## Desenvolvimento

- App: [`client/README.md`](client/README.md) — build para PS5 com o
  [ps5-payload-sdk](https://github.com/ps5-payload-dev/sdk) e uma build para
  desktop com o SDL2 do Homebrew, controlada por um arquivo de comandos para
  testar a interface sem um console. As traduções ficam em `client/i18n/` (um
  JSON por idioma).
- Servidor: [`server/README.md`](server/README.md) — Node.js ≥ 20 e PostgreSQL,
  testes end-to-end com `npm test`.
- Arquitetura: [`docs/architecture.md`](docs/architecture.md).

Contribuições são bem-vindas: veja o [CONTRIBUTING.md](CONTRIBUTING.md).

## Autor

O Omega é desenvolvido por **TheCriicom** — [outlinedigital.it](https://outlinedigital.it).

## Licença

Copyright © 2026 TheCriicom e os colaboradores do Omega.
O Omega é software livre: [GNU GPL v3 ou posterior](LICENSE). Os componentes de
terceiros e suas licenças estão listados em
[`client/THIRD-PARTY-NOTICES.md`](client/THIRD-PARTY-NOTICES.md).

O Omega é um projeto independente e não é afiliado, endossado nem patrocinado
pela Sony Interactive Entertainment. "PlayStation" e "PS5" são marcas
registradas de seus respectivos proprietários. O Omega não contém nem distribui
jogos.
