# Api di Omega. Oltre alle dipendenze npm servono ffmpeg (avatar e copertine)
# e JxrDecApp per le catture PS5 in JPEG XR: Alpine non lo pacchettizza, quindi
# si compila da jxrlib nel primo stadio.
FROM node:22-alpine AS jxr
RUN apk add --no-cache git build-base \
 && git clone -q --depth 1 https://github.com/4creators/jxrlib /j \
 && cd /j && make -j4 CFLAGS="-O2 -w -fPIC -D__ANSI__ -DDISABLE_PERF_MEASUREMENT -std=gnu89 -fpermissive -I. -Icommon/include -Iimage/sys -Ijxrgluelib -Ijxrtestlib" \
 && cp build/JxrDecApp /JxrDecApp

FROM node:22-alpine
# tar GNU per l'archivio del sorgente servito su /source
RUN apk add --no-cache ffmpeg tar
COPY --from=jxr /JxrDecApp /usr/local/bin/JxrDecApp
WORKDIR /app
COPY package.json package-lock.json* ./
RUN if [ -f package-lock.json ]; then npm ci --omit=dev; else npm install --omit=dev; fi \
 && npm cache clean --force
COPY config ./config
COPY migrations ./migrations
COPY src ./src
COPY store-assets ./store-assets
COPY admin-ui ./admin-ui
# web app per telefono e PC (/app)
COPY webapp ./webapp
# foto e video caricati dagli utenti (volume media-data) e salvataggi cifrati (saves-data)
RUN mkdir -p /app/media /app/saves && chown node:node /app/media /app/saves
USER node
EXPOSE 8080
CMD ["node", "src/server.js"]
