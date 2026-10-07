-- Preferenze delle notifiche: per tipo (tutti, solo preferiti, nessuno), amici
-- preferiti o silenziati, orari di silenzio e cosa mostrare durante il gioco.
-- Una notifica che non si vuole non si crea; quelle arrivate negli orari di
-- silenzio o mentre si gioca restano nell'elenco ma senza avviso (silent).
ALTER TABLE lab_account ADD COLUMN IF NOT EXISTS notify_prefs JSONB NOT NULL DEFAULT '{}';
ALTER TABLE lab_notification ADD COLUMN IF NOT EXISTS silent BOOLEAN NOT NULL DEFAULT false;
CREATE TABLE IF NOT EXISTS lab_notify_friend (
  account_id BIGINT NOT NULL REFERENCES lab_account(account_id) ON DELETE CASCADE,
  friend_id  BIGINT NOT NULL REFERENCES lab_account(account_id) ON DELETE CASCADE,
  favorite   BOOLEAN NOT NULL DEFAULT false,
  muted      BOOLEAN NOT NULL DEFAULT false,
  PRIMARY KEY (account_id, friend_id)
);
