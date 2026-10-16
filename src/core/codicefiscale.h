#pragma once

#include <QString>

namespace mdr {

// Codice fiscale del cliente da stampare sul documento commerciale (spese sanitarie detraibili):
// codice fiscale di una persona (16 caratteri, anche omocodico) o partita IVA (11 cifre).
// La tessera sanitaria ha il codice fiscale anche come codice a barre: il lettore lo "scrive".

// Maiuscolo, senza spazi né trattini.
QString normalizzaCodiceFiscale(const QString& testo);

// Formato e carattere (o cifra) di controllo. Il testo va prima normalizzato.
bool codiceFiscaleValido(const QString& codice);

} // namespace mdr
