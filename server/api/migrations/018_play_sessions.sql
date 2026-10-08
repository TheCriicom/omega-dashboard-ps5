-- Il tuo riepilogo (stile Wrap-Up): ogni tratto di gioco chiuso da
-- src/playtime.js finisce anche qui, con inizio e fine. Servono il ritmo
-- (ore del giorno, giorni della settimana), la sessione più lunga, i giorni di
-- fila e chi ha giocato insieme a chi. I totali restano in lab_playtime e
-- lab_playtime_week; queste righe si cancellano dopo 400 giorni.
CREATE TABLE IF NOT EXISTS lab_play_session (
  session_id  BIGSERIAL PRIMARY KEY,
  account_id  BIGINT NOT NULL REFERENCES lab_account(account_id) ON DELETE CASCADE,
  game_id     TEXT NOT NULL,
  started_at  TIMESTAMPTZ NOT NULL,
  ended_at    TIMESTAMPTZ NOT NULL,
  seconds     INT NOT NULL
);
CREATE INDEX IF NOT EXISTS idx_play_session_acct ON lab_play_session(account_id, ended_at);
CREATE INDEX IF NOT EXISTS idx_play_session_game ON lab_play_session(game_id, ended_at);
