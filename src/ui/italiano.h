#pragma once

class QCoreApplication;

namespace mdr {

// Testi standard di Qt in italiano (pulsanti delle finestre, anteprima di stampa, menu dei campi
// di testo) e lingua italiana per date e numeri. Le traduzioni di Qt non sono nel pacchetto:
// questo copre quelle che il programma mostra davvero, senza file .qm da distribuire.
void usaItaliano(QCoreApplication& app);

// Traduzione di un testo standard di Qt ("&Cancel" → "&Annulla"); vuoto se non la conosce.
const char* testoItaliano(const char* inglese);

} // namespace mdr
