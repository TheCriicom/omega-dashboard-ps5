<p align="center">
  <picture><source media="(prefers-color-scheme: dark)" srcset="docs/img/omega-mark.svg"><img src="docs/img/omega-mark-light.svg" width="96" alt="Omega"></picture>
</p>

<h1 align="center">Omega</h1>

<p align="center">
  홈브루가 가능한 PS5를 위한 오픈 소스 대시보드: 게임, Store, 음악, 친구, Party를 한곳에서.<br>
  개발: <a href="https://outlinedigital.it"><b>TheCriicom</b></a>.
</p>

<p align="center"><sub><a href="README.md">English</a> · <a href="README.it.md">Italiano</a> · <a href="README.ja.md">日本語</a> · <a href="README.fr.md">Français</a> · <a href="README.es.md">Español</a> · <a href="README.de.md">Deutsch</a> · <a href="README.nl.md">Nederlands</a> · <a href="README.pt-PT.md">Português (Portugal)</a> · <a href="README.pt-BR.md">Português (Brasil)</a> · <a href="README.ru.md">Русский</a> · <b>한국어</b> · <a href="README.zh-Hans.md">简体中文</a> · <a href="README.zh-Hant.md">繁體中文</a> · <a href="README.fi.md">Suomi</a> · <a href="README.sv.md">Svenska</a> · <a href="README.da.md">Dansk</a> · <a href="README.nb.md">Norsk bokmål</a> · <a href="README.pl.md">Polski</a> · <a href="README.tr.md">Türkçe</a> · <a href="README.cs.md">Čeština</a> · <a href="README.hu.md">Magyar</a> · <a href="README.el.md">Ελληνικά</a> · <a href="README.ro.md">Română</a> · <a href="README.th.md">ไทย</a> · <a href="README.vi.md">Tiếng Việt</a> · <a href="README.id.md">Bahasa Indonesia</a> · <a href="README.uk.md">Українська</a></sub></p>

<p align="center">
  <a href="https://play.omegasuite.it">웹사이트</a> ·
  <a href="https://play.omegasuite.it/installa">설치</a> ·
  <a href="server/README.md">서버 호스팅</a> ·
  <a href="client/README.md">개발</a>
</p>

![Omega Home](docs/screenshots/home.png)

Omega는 홈브루 사용자를 위해 만든 대시보드이며, 원한다면 Home 화면으로도 쓸 수
있습니다(처음 실행할 때 물어봅니다). 설치된 게임과 홈브루가 한 줄에 함께
놓이고, Store는 버튼 하나로 설치하며, 게임 중에도 음악이 계속 재생되고, 친구는
언제나 버튼 한 번이면 만날 수 있습니다. Omega는 홈브루(SDL2, 소프트웨어
렌더러)로 실행되며, 누구나 호스팅할 수 있는 서버와 통신합니다.

## 주요 기능

- **Home**: 게임과 홈브루를 함께 표시, 아트워크에서 만든 동적 배경, 바로 실행
  (게임은 LncUtil, 홈브루는 websrv, ELF 페이로드는 백그라운드로).
- **Store**: 오픈 소스 홈브루 모음. 검색, 카테고리별 진열대, 투표, 평점,
  댓글을 지원합니다. `.pkg`, `.zip`, `.elf`를 스스로 인식해 각각 알맞은 위치에
  설치합니다.
- **내 라이브러리**: 내가 소유한 게임의 백업을 다운로드 링크와 커버와 함께
  관리합니다. 콘솔이나 휴대폰에서 추가하거나 JSON 파일에서 가져올 수 있으며
  (한 번만 가져오거나, 연결해 두고 계속 동기화), 콘솔에만 저장됩니다.
- **음악**, 게임 중에도: 인터넷 라디오(radio-browser), Navidrome 및 기타
  Subsonic 서버, USB 파일, 모든 오디오 링크. 재생은 백그라운드 데몬(오디오 전용
  FFmpeg 빌드)에서 이루어지므로 게임을 실행해도 멈추지 않습니다.
- **휴대폰 리모컨**: 데몬이 9095 포트에서 웹 페이지를 제공합니다. 콘솔에 표시된
  QR 코드를 스캔하고 PIN을 입력하면, 어떤 휴대폰, 태블릿, PC 브라우저에서든
  음악을 제어하고, 오디오 파일을 보내고, 내 라이브러리를 관리할 수 있습니다.
- **HEN 감지**: 시작할 때 Omega가 OnionHEN, etaHEN, pldmgr, ps5_autoloader를
  인식하고, 빠진 것을 알려 주며, 확인하면 해당 서비스를 알맞은 위치에 설치하고
  활성화합니다. OnionHEN에서는 게임 중 메뉴(L2 + R3)에 음악 컨트롤이 있는
  페이지를 추가합니다.
- **시스템 도구**: 온도, 팬 임계값, 저장 공간, 파일 관리자.
- **Community**: 게시물, 좋아요, 댓글이 있는 담벼락; 그룹 채팅; 알 수도 있는
  사람; 플레이 시간과 친구 순위표.
- **Party**: 채팅과 음성(Opus), 게임 초대, 사용자 지정 상태(온라인, 자리 비움,
  방해 금지, 오프라인으로 표시).
- **Game Base**: 친구, 친구 요청, 메시지.
- **개인정보 보호**: 차단, 신고, 메시지를 보낼 수 있는 사람 설정, 데이터
  내보내기, 계정 삭제.
- **테마**, 생성형 배경 음악, 읽기용 브라우저.
- **27개 언어**: 앱이 콘솔 언어를 자동으로 따르며, 설정에서 바꿀 수도 있습니다.
- **서버 선택**: 설정에서 원하는 Omega 서버의 주소를 추가할 수 있으며, 공식
  서버는 항상 사용할 수 있습니다.

| | |
|---|---|
| ![Store](docs/screenshots/store.png) | ![Community](docs/screenshots/community.png) |
| ![Party](docs/screenshots/party.png) | ![플레이 시간](docs/screenshots/stats.png) |
| ![PC에서 본 휴대폰 리모컨](docs/screenshots/remote-music.png) | ![PC에서 본 내 라이브러리](docs/screenshots/remote-library.png) |

## 저장소 구성

| 폴더 | |
|---|---|
| [`client/`](client) | 콘솔 앱(C, SDL2)과 Mac에서 개발하기 위한 데스크톱 빌드 |
| [`daemon/`](daemon) | 백그라운드 페이로드: 음악 플레이어, 휴대폰 리모컨 웹 페이지, 내 라이브러리, 게임 중 알림, 그리고 (선택한 경우에만) Home으로 돌아가면 Omega를 다시 열기 |
| [`onionhen-plugin/`](onionhen-plugin) | OnionHEN 플러그인: 게임 중 메뉴에 음악 컨트롤이 있는 Omega 페이지 |
| [`server/`](server) | Node.js API, 프록시, 관리(모더레이션) 패널, 서버 호스팅용 Docker Compose |
| [`docs/`](docs) | 아키텍처 문서와 이미지 |

## 콘솔에 Omega 설치하기

홈브루를 지원하는 탈옥된 PS5가 필요합니다. OnionHEN이나 etaHEN 같은 HEN, 또는
[websrv](https://github.com/ps5-payload-dev/websrv) 같은 런처와 페이로드 로더면
됩니다.
가장 쉬운 방법은 콘솔의 브라우저에서
[play.omegasuite.it/installa](https://play.omegasuite.it/installa)를 여는
것입니다. 또는 웹사이트에서 패키지를 내려받아 FTP로 `data/`를 콘솔에
복사하세요.

## 서버 호스팅하기

```sh
git clone https://github.com/CristianLaporta/omega-dashboard-ps5.git
cd omega-dashboard-ps5/server
./scripts/install.sh
```

스크립트가 무작위 시크릿으로 `.env`를 만들고 컨테이너를 시작합니다. Caddy를
통한 자동 HTTPS를 포함한 전체 가이드는
[`server/README.md`](server/README.md)에 있습니다. 그런 다음 콘솔에서
**설정 → 서버 → 서버 추가**로 이동하세요.

## 개발

- 앱: [`client/README.md`](client/README.md) —
  [ps5-payload-sdk](https://github.com/ps5-payload-dev/sdk)를 이용한 PS5 빌드와,
  Homebrew의 SDL2를 이용한 데스크톱 빌드. 데스크톱 빌드는 명령 파일로 조작할 수
  있어 콘솔 없이 UI를 테스트할 수 있습니다. 번역 파일은 `client/i18n/`에
  있습니다(언어당 JSON 하나).
- 서버: [`server/README.md`](server/README.md) — Node.js ≥ 20과 PostgreSQL,
  `npm test`로 엔드투엔드 테스트.
- 아키텍처: [`docs/architecture.md`](docs/architecture.md).

기여를 환영합니다. [CONTRIBUTING.md](CONTRIBUTING.md)를 참고하세요.

## 개발자

Omega는 **TheCriicom**이 개발합니다 — [outlinedigital.it](https://outlinedigital.it).

## 라이선스

Copyright © 2026 TheCriicom 및 Omega 기여자.
Omega는 자유 소프트웨어입니다: [GNU GPL v3 이상](LICENSE). 서드파티 구성 요소와
그 라이선스는
[`client/THIRD-PARTY-NOTICES.md`](client/THIRD-PARTY-NOTICES.md)에 나와 있습니다.

Omega는 독립 프로젝트이며 Sony Interactive Entertainment와 제휴되어 있지
않고, 그 승인이나 후원을 받지 않습니다. "PlayStation"과 "PS5"는 각 소유자의
상표입니다. Omega는 게임을 포함하거나 배포하지 않습니다.
