# Sicurezza

## Segnalare un problema

Problemi di sicurezza **in privato**: scheda *Security* del repository → *Report a vulnerability*.
Non aprire una issue pubblica. Si risponde appena possibile; la correzione arriva alle casse e ai
palmari con l'aggiornamento automatico.

## Cosa conta

- Accesso ai dati del negozio senza account valido, o con più permessi del proprio ruolo.
- Aggiornamenti di casse e palmari: sono firmati con la chiave del progetto e una release non
  firmata deve essere scartata. Un modo per farne accettare una non firmata è un problema grave.
- Vendite, magazzino e documenti fiscali alterabili senza passare dalle funzioni del database.

## Cosa non conta

- Chiavi, password e partite IVA che si trovano nel codice: sono **solo di prova** (server di
  sviluppo e test), mai di un server vero.
- La chiave anon del server (`/mdr/presentazione`): è pubblica per costruzione; i dati li
  proteggono gli account e le regole del database.
