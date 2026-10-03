'use strict';
// Testi legali del servizio (privacy, termini, licenze): un'unica fonte per
// l'app (JSON a sezioni) e per il web (HTML). Descrivono ciò che il servizio
// fa davvero: se cambiano i dati raccolti, si aggiornano qui e TERMS_VERSION.
const config = require('./config');

const TERMS_VERSION = '2026-10-03';
const LOG_RETENTION_DAYS = config.logRetentionDays;
const PUBLIC_URL = config.publicUrl;

// Dati del titolare dal .env (LEGAL_*): finché mancano si mostra "[da completare]".
function controller() {
  const l = config.legal;
  const name = l.controllerName;
  const email = l.contactEmail;
  return {
    name, vat: l.controllerVat, email, address: l.controllerAddress, hosting: l.hostingProvider,
    complete: !!(name && email),
  };
}
const or = (v, fallback = '[da completare]') => v || fallback;

function privacy() {
  const c = controller();
  return {
    title: 'Informativa sulla privacy',
    sections: [
      { title: 'Chi tratta i tuoi dati', body:
        `Titolare del trattamento: ${or(c.name)}${c.vat ? `, P.IVA ${c.vat}` : ''}${c.address ? `, ${c.address}` : ''}. ` +
        `Contatto per la privacy e per le segnalazioni: ${or(c.email)}. ` +
        'Omega è un progetto indipendente: non è collegato a Sony Interactive Entertainment e non usa i suoi account né i suoi server.' },
      { title: 'Quali dati raccogliamo', body:
        'Account: ID online, password (conservata solo come impronta scrypt, mai in chiaro), email se la indichi, avatar e copertina che carichi, testo del profilo, data di iscrizione e ultimo accesso. ' +
        'Funzioni social: amicizie e richieste, stato online e titolo che stai usando (inviato dalla tua console mentre giochi), attività recenti, messaggi diretti, chat e inviti dei party, notifiche. ' +
        'Store: gli homebrew che pubblichi, commenti, mi piace/non mi piace, valutazioni, segnalazioni, numero di installazioni. ' +
        'Libreria personale: l\'indirizzo del tuo file JSON e l\'elenco dei titoli che contiene (nome, immagini, link). ' +
        `Dati tecnici: per ogni richiesta al server registriamo indirizzo IP, data e ora, percorso richiesto (incluso l'indirizzo delle pagine aperte col browser Omega), esito e tempi. Password, token e cookie vengono oscurati e non vengono mai salvati nei registri.` },
      { title: 'Perché e su quale base', body:
        'Per fornirti il servizio che hai chiesto (account, social, Store, libreria): esecuzione del contratto, art. 6.1.b GDPR. ' +
        'Per la sicurezza, prevenire abusi e gestire le segnalazioni: legittimo interesse e obblighi di legge sui contenuti ospitati, art. 6.1.f e 6.1.c. ' +
        'Non usiamo i tuoi dati per pubblicità o profilazione e non li vendiamo.' },
      { title: 'Per quanto tempo', body:
        `I registri tecnici delle richieste si cancellano dopo ${LOG_RETENTION_DAYS} giorni. ` +
        'I dati dell\'account e i contenuti restano finché non elimini l\'account; le sessioni scadono dopo 24 ore. ' +
        'Le segnalazioni chiuse e il registro delle azioni di moderazione si conservano finché servono a dimostrare la gestione dei contenuti.' },
      { title: 'Chi può vederli', body:
        'Gli altri utenti vedono il tuo profilo pubblico, ciò che pubblichi e, se siete amici, la tua presenza e attività. I messaggi li vedono solo i destinatari. ' +
        `I dati stanno su un server gestito dal titolare${c.hosting ? ` presso ${c.hosting}` : ''}, che opera come responsabile del trattamento. ` +
        'Quando installi un homebrew o apri una pagina web, la console o il nostro server si collegano al sito che ospita quel file o quella pagina: vale l\'informativa di quel sito.' },
      { title: 'I tuoi diritti', body:
        'Puoi accedere ai tuoi dati e scaricarli (Impostazioni → Privacy e dati → Scarica i miei dati), correggerli, cancellarli (Elimina account), opporti al trattamento o chiederne la limitazione, ' +
        `scrivendo a ${or(c.email)}. Puoi anche presentare reclamo al Garante per la protezione dei dati personali (www.garanteprivacy.it).` },
      { title: 'Età minima', body: 'Il servizio è riservato a chi ha almeno 14 anni.' },
      { title: 'Versione', body: `Aggiornata il ${TERMS_VERSION}.` },
    ],
  };
}

function terms() {
  const c = controller();
  return {
    title: 'Termini d\'uso',
    sections: [
      { title: 'Il servizio', body:
        `Omega (${PUBLIC_URL}) offre gratuitamente account, funzioni social e uno Store di homebrew per console su cui l'utente ha legittimamente installato software di terze parti. ` +
        `È gestito da ${or(c.name)}. Omega è un progetto indipendente e non è affiliato, approvato o sponsorizzato da Sony Interactive Entertainment. "PlayStation" e "PS5" sono marchi dei rispettivi titolari.` },
      { title: 'Uso a tuo rischio', body:
        'Il servizio e gli homebrew dello Store sono forniti così come sono. Installare software non ufficiale può causare malfunzionamenti, perdita di dati o la perdita della garanzia della console, ed è a tuo rischio. ' +
        'Gli homebrew sono opere dei rispettivi autori, con le loro licenze.' },
      { title: 'Cosa non è consentito', body:
        'Pubblicare o condividere contenuti di cui non hai i diritti — in particolare copie di giochi o software commerciali, link a materiale piratato, strumenti per aggirarne le protezioni a fini di pirateria —, ' +
        'malware o file pericolosi, contenuti illeciti, offensivi o che incitano all\'odio, spam, l\'uso di account altrui o il tentativo di comprometterne la sicurezza.' },
      { title: 'Pubblicare nello Store', body:
        'Pubblicando dichiari di essere l\'autore dell\'homebrew o che la sua licenza ne permette la redistribuzione, e che il link porta al file indicato senza modifiche dannose. ' +
        'Ci autorizzi a mostrare nome, descrizione e immagini nello Store. I file restano sul sito che indichi: Omega ne ospita solo la scheda.' },
      { title: 'Libreria personale', body:
        'La libreria serve a organizzare e reinstallare i backup dei titoli che possiedi legittimamente, per uso personale. ' +
        'Non usarla per condividere copie con altri: i file restano dove li tieni tu e Omega non li ospita.' },
      { title: 'Segnalazioni e moderazione', body:
        `Puoi segnalare un homebrew o un commento dallo Store (Opzioni → Segnala) o scrivendo a ${or(c.email)}. ` +
        'Esaminiamo le segnalazioni e possiamo oscurare o rimuovere contenuti e sospendere o chiudere account che violano questi termini; i contenuti con più segnalazioni vengono oscurati in attesa di verifica.' },
      { title: 'Responsabilità', body:
        'Nei limiti consentiti dalla legge non rispondiamo dei danni derivanti dall\'uso degli homebrew o dall\'indisponibilità del servizio, che può cambiare o interrompersi. Restano ferme le tutele inderogabili previste per i consumatori.' },
      { title: 'Modifiche e legge applicabile', body:
        'Se i termini cambiano te lo chiederemo di nuovo nell\'app. Si applica la legge italiana.' },
      { title: 'Versione', body: `Termini in vigore dal ${TERMS_VERSION}.` },
    ],
  };
}

function licenses() {
  return {
    title: 'Licenze open source',
    sections: [
      { title: 'Omega', body:
        `L'app Omega per console è software libero distribuito con licenza GNU GPL versione 3 o successive, senza alcuna garanzia. Il codice sorgente completo si scarica da ${PUBLIC_URL}/source.` },
      { title: 'Componenti usati dall\'app', body:
        'ps5-payload-sdk (GPL-3.0 o successive) · SDL2, SDL2_ttf, SDL2_image (zlib) · FreeType (FreeType License) · HarfBuzz (MIT) · libpng (libpng License) · libjpeg (IJG) · libwebp (BSD-3-Clause) · zlib e minizip (zlib) · bzip2 (bzip2 License) · QuickJS (MIT).' },
      { title: 'Componenti usati dal server', body:
        'Node.js (MIT) · node-postgres (MIT) · Mozilla Readability (Apache-2.0) · linkedom (ISC) · PostgreSQL (PostgreSQL License) · FFmpeg (LGPL-2.1 o successive) · jxrlib (BSD-2-Clause).' },
      { title: 'Homebrew dello Store', body:
        'Ogni homebrew del catalogo è un progetto open source con la propria licenza, indicata nella sua scheda insieme al sito del progetto.' },
    ],
  };
}

function all() {
  const c = controller();
  return {
    version: TERMS_VERSION,
    controller: { name: c.name, vat: c.vat, email: c.email, complete: c.complete },
    source_url: `${PUBLIC_URL}/source`,
    documents: { privacy: privacy(), terms: terms(), licenses: licenses() },
  };
}

module.exports = { TERMS_VERSION, all, privacy, terms, licenses };
