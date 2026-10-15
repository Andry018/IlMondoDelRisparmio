---
name: systemapp-db
description: "Dove sono i database SQL Server di SystemApp copiati dall'utente e come sono collegati in LocalDB per l'importazione"
metadata:
  node_type: memory
  type: project
  originSessionId: 16c187a4-07e4-453f-bbfc-27c466da0741
  modified: 2026-10-14T18:20:11.995Z
---

Copia dei dati SystemApp su chiavetta USB D: (14/10/2026): D:\SApp_Store_DATI (MDR_Dati = anagrafiche, MDR_2020..2026 = movimenti per anno, MDR_Fatt_Elett...), D:\SApp_Cash_DATI (SAPP_*), formato SQL Server 2014 (istanza SQLSYSTEMAPP).
Installato SQL Server LocalDB 2025 (istanza `(localdb)\SystemApp`); MDR_Dati, MDR_2025 e MDR_2026 collegati da copie in C:\Users\andre\SystemApp_import (da D: l'aggancio fallisce: chiavetta USB, errore 21). Gli originali su D: non sono toccati.

Piano in docs/MIGRAZIONE_SYSTEMAPP.md. Decisioni dell'utente (15/10/2026): giacenze negative portate cosi' come sono; storico vendite solo 2025 e 2026; script scritti e provati il 15/10/2026 (strumenti/importa-systemapp: estrai.ps1 + carica.ps1/carica.sql), piu' 'Importa da file' (CSV/xlsx) in mdr-gestione.

**Why:** importazione dati da SystemApp nel nuovo gestionale; dati reali di clienti.

**How to apply:** lavorare sempre su copie in C:\Users\andre\SystemApp_import, mai nel repo né su GitHub; non leggere password (TAB_Operatori_SFX.pass). Vedi [[consegne-frontend]].
