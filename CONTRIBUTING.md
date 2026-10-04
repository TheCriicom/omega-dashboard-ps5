<sub>**English** · [Italiano](CONTRIBUTING.it.md)</sub>

# Contributing to Omega

Thanks for your interest. A few guidelines so we can work well together.

## Before you start

- For a bug, open an issue with the steps to reproduce it, the Omega version
  (Settings) and, if the console is involved, the relevant lines of
  `/data/Omega/omega-ui.log`.
- For a new feature, open an issue first: we discuss it and avoid wasted
  work.

## Code

- **App** (`client/`): C17, compact style like the existing code, comments in
  Italian and only where they explain the why. Test your changes with the
  desktop build (`client/desktop/build-desktop.sh`) and, if you touch system
  calls, on the console too.
- **Server** (`server/api/`): Node.js ≥ 20, CommonJS, no new dependencies
  without a good reason. Every new route goes in `config/endpoints.json`; every
  schema change is a new migration (existing ones are never modified).
- User-facing text is written in Italian in the code and wrapped in `_()`; translations live in
  `client/i18n/<lang>.json`. Run `node client/tools/i18n-extract.mjs` after adding strings and
  keep printf specifiers identical in every translation. New languages are welcome.
- `npm test` in `server/api` must pass (see `server/README.md`).

## Pull requests

- One PR per topic, with a description of what changes and why.
- Include screenshots if the interface changes (the desktop build has the
  `shot` command in the command file).
- By contributing, you agree that your code is distributed under the
  GPL-3.0-or-later license.

## What we don't accept

Code to circumvent game protections for piracy, distribution of games or
commercial software, collection of credentials or unnecessary data.
