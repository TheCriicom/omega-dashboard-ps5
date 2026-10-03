# Proxy di Omega: solo moduli core di Node, nessuna dipendenza npm.
FROM node:22-alpine
WORKDIR /app
COPY package.json ./
COPY src ./src
# la cartella dei log è un volume: deve esistere ed essere dell'utente node
RUN mkdir -p /var/log/omega && chown node:node /var/log/omega
USER node
EXPOSE 9986
CMD ["node", "src/server.js"]
