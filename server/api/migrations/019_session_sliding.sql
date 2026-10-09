-- Le sessioni ora scadono per inattività (src/session.js le rinnova a ogni uso).
-- Fino a qui scadevano 24 ore dopo il login anche per chi usava Omega di
-- continuo: chi è stato buttato fuori così negli ultimi 30 giorni torna dentro
-- con la sessione che ha già sulla console. Le sessioni chiuse con un logout
-- (revoked) restano chiuse.
UPDATE lab_session
   SET expires_at = now() + interval '90 days'
 WHERE NOT revoked AND expires_at > now() - interval '30 days';
