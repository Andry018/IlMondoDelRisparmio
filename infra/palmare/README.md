# Palmare: versione web sul server del negozio

Sui palmari si installa l'**APK** (`palmare/README.md`): si apre anche senza rete e tiene una copia
degli articoli. Questa cartella serve la **stessa app in versione web** sulla porta **8080**, utile
da PC (es. il responsabile che controlla un inventario) e come alternativa su un telefono qualsiasi:
si apre dal browser e si aggiorna da sola quando cambiano i file sul server.

## Sul server

```bash
cd infra/palmare
cp config.esempio.json config.json
nano config.json          # indirizzo del server (porta 8000 di Supabase) e ANON_KEY dal .env
docker compose up -d
```

`config.json` contiene solo la chiave **pubblica** (ANON_KEY), come le casse: da sola non dà
accesso a nulla, serve comunque il login dell'operatore. Non metterci mai la SERVICE_ROLE_KEY.

Prova dal PC: `http://<ip-server>:8080`.

## Versione web su un telefono o palmare (alternativa all'APK)

1. Collegare il palmare al Wi-Fi del negozio.
2. Aprire Chrome su `http://<ip-server>:8080` → menu ⋮ → **Aggiungi a schermata Home**.
3. Impostare il lettore di codici a barre in **modalità tastiera** (keyboard wedge /
   "emulazione tastiera") con **Invio dopo la lettura**: l'app riceve il codice come se fosse
   digitato. Su molti terminali si fa dall'app "Scanner" o "Impostazioni scanner".
4. Accedere con email e password dell'operatore (magazziniere o responsabile per inventario e
   ricevimento; anche il cassiere per verifica prezzo ed etichette).

Ogni operatore entra con il proprio account: le conte e le spunte riportano chi le ha fatte.

## Rete senza https: cosa cambia

Nella rete del negozio l'app gira in `http`. Il browser in questo caso non permette:
- la **fotocamera** come lettore (serve il lettore integrato del palmare);
- l'**apertura dell'app senza rete** (la pagina va caricata con il Wi-Fi acceso).

Le conte e le spunte fatte senza Wi-Fi restano comunque sul palmare e partono da sole quando la
rete torna. Se in futuro servono fotocamera o avvio offline, si aggiunge un certificato https
(per esempio con una CA interna installata sui palmari).

## Inventario: come usarlo

1. Il responsabile apre l'inventario (dal palmare o dalla gestione), indicando i reparti o tutto
   il negozio.
2. Gli operatori leggono gli articoli, ognuno con la sua ubicazione (corsia, magazzino…). Le
   conte dello stesso articolo si sommano; "Ogni lettura conta 1 pezzo" velocizza la conta.
3. Il responsabile controlla il riepilogo e chiude: il server registra le rettifiche.

La differenza si calcola sulla giacenza nel momento della conta, quindi si può contare anche a
negozio aperto. Un articolo esposto in più punti va contato in tutti i punti in un tempo breve:
una vendita tra la conta dello scaffale e quella del magazzino verrebbe contata due volte.
