#pragma once

#include <QByteArray>

namespace mdr {

// Verifica una firma RSA PKCS#1 v1.5 su SHA-256 (quella di "openssl dgst -sha256 -sign").
// modulo: big-endian, come lo stampa "openssl rsa -noout -modulus" (decodificato da esadecimale).
// Usa la crittografia di Windows (CNG): nessuna libreria in più da distribuire.
bool verificaFirma(const QByteArray& dati, const QByteArray& firma, const QByteArray& modulo,
                   quint32 esponente = 65537);

// Chiave pubblica degli aggiornamenti, compilata nel programma (chiaveaggiornamenti.h).
bool verificaFirmaAggiornamento(const QByteArray& dati, const QByteArray& firma);

} // namespace mdr
