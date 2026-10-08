#include "firma.h"

#include "chiaveaggiornamenti.h"

#include <QCryptographicHash>

#ifdef Q_OS_WIN
#include <windows.h>
#include <bcrypt.h>
#endif

namespace mdr {

bool verificaFirma(const QByteArray& dati, const QByteArray& firma, const QByteArray& modulo, quint32 esponente)
{
#ifdef Q_OS_WIN
    if (modulo.isEmpty() || firma.size() != modulo.size())
        return false;

    // BCRYPT_RSAKEY_BLOB seguito da esponente e modulo, entrambi big-endian.
    QByteArray esp;
    for (quint32 e = esponente; e > 0; e >>= 8)
        esp.prepend(char(e & 0xff));
    BCRYPT_RSAKEY_BLOB testa{};
    testa.Magic = BCRYPT_RSAPUBLIC_MAGIC;
    testa.BitLength = ULONG(modulo.size() * 8);
    testa.cbPublicExp = ULONG(esp.size());
    testa.cbModulus = ULONG(modulo.size());
    QByteArray blob(reinterpret_cast<const char*>(&testa), sizeof(testa));
    blob += esp;
    blob += modulo;

    BCRYPT_ALG_HANDLE algoritmo = nullptr;
    if (!BCRYPT_SUCCESS(BCryptOpenAlgorithmProvider(&algoritmo, BCRYPT_RSA_ALGORITHM, nullptr, 0)))
        return false;
    BCRYPT_KEY_HANDLE chiave = nullptr;
    bool ok = BCRYPT_SUCCESS(BCryptImportKeyPair(algoritmo, nullptr, BCRYPT_RSAPUBLIC_BLOB, &chiave,
                                                 reinterpret_cast<PUCHAR>(blob.data()), ULONG(blob.size()), 0));
    if (ok) {
        QByteArray impronta = QCryptographicHash::hash(dati, QCryptographicHash::Sha256);
        QByteArray f = firma;
        BCRYPT_PKCS1_PADDING_INFO padding{BCRYPT_SHA256_ALGORITHM};
        ok = BCRYPT_SUCCESS(BCryptVerifySignature(chiave, &padding,
                                                  reinterpret_cast<PUCHAR>(impronta.data()), ULONG(impronta.size()),
                                                  reinterpret_cast<PUCHAR>(f.data()), ULONG(f.size()),
                                                  BCRYPT_PAD_PKCS1));
        BCryptDestroyKey(chiave);
    }
    BCryptCloseAlgorithmProvider(algoritmo, 0);
    return ok;
#else
    Q_UNUSED(dati);
    Q_UNUSED(firma);
    Q_UNUSED(modulo);
    Q_UNUSED(esponente);
    return false;   // gli aggiornamenti automatici esistono solo su Windows
#endif
}

bool verificaFirmaAggiornamento(const QByteArray& dati, const QByteArray& firma)
{
    return verificaFirma(dati, firma, QByteArray::fromHex(chiavePubblicaModulo), chiavePubblicaEsponente);
}

} // namespace mdr
