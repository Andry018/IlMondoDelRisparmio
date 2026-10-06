# Mondo del Risparmio — gestionale negozio

Cassa, magazzino, anagrafiche e fatturazione per un punto vendita con più casse.
Sostituisce SystemApp Cash e Terminal. Architettura in [docs/ARCHITETTURA.md](docs/ARCHITETTURA.md).

- **Server:** Supabase self-hosted nella rete del negozio ([infra/supabase](infra/supabase/README.md))
- **Casse e PC:** applicazione Windows in C++ / Qt 6
- **Palmare:** futuro, sulle stesse API

## Sviluppo (Windows)

Prerequisiti, da MSYS2 UCRT64:

```bash
pacman -S mingw-w64-ucrt-x86_64-{gcc,cmake,ninja,qt6-base,qt6-websockets,postgresql}
```

Compilare e lanciare i test C++ (PowerShell):

```powershell
$env:PATH = "C:\msys64\ucrt64\bin;$env:PATH"
cmake --preset msys2-debug
cmake --build --preset debug
ctest --preset debug
```

Test del database (avvia un PostgreSQL temporaneo, applica le migrazioni e prova i flussi):

```powershell
.\supabase\tests\esegui_test.ps1
```
