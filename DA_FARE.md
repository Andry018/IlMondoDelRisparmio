# Da fare

## Rinominare la cartella in IlMondoDelRisparmio

1. Chiudere Claude Code, editor e terminali aperti in questa cartella.
2. Da un terminale (cmd) sul Desktop:
   ```
   cd C:\Users\andre\Desktop
   ren MondoDelRisparmio IlMondoDelRisparmio
   ```
3. Rigenerare la build (la cache CMake contiene il vecchio percorso assoluto):
   ```
   cd IlMondoDelRisparmio
   rmdir /s /q build
   cmake --preset debug
   ```
4. Riaprire Claude Code dalla nuova cartella.

Git non richiede modifiche: remote `origin` = https://github.com/Andry018/IlMondoDelRisparmio.git

## Sull'altra macchina

```
git clone https://github.com/Andry018/IlMondoDelRisparmio.git
cd IlMondoDelRisparmio
cmake --preset debug
```
