// omega_redirect — spegnimento, riavvio e modalità riposo (power.c).
//
// Un thread legge ogni 100 ms il flag di sistema SceSystemStateMgrInfo (come
// ShadowMountPlus e OnionHEN, GPL-3): quando la console sta per spegnersi o
// addormentarsi il demone chiude audio, microfono, rete e montaggi prima che
// lo faccia il sistema, e riparte da solo al risveglio.
#pragma once

void power_start(void);       // dal main, prima degli altri thread
int  power_sleeping(void);    // 1 durante riposo/spegnimento: niente rete, audio o scritture nuove
int  power_stopping(void);    // 1 se la console si sta spegnendo o riavviando: il main esce
void power_prepare(void);     // come l'ingresso in riposo, a richiesta (UI prima di spegnere): torna a lavoro finito
void power_resume(void);      // annulla power_prepare se la richiesta di spegnimento non è andata a buon fine
