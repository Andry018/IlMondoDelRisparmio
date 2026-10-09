#include "vistaserver.h"

#include <QComboBox>
#include <QJsonArray>
#include <QMessageBox>
#include <QPointer>

namespace mdr {

namespace {
const QString campiArticolo = QStringLiteral("id,codice,descrizione,unita,prezzo_vendita,costo_ultimo");
} // namespace

void cercaArticoloServer(ClientServer& client, const QString& codice, QObject* contesto,
                         std::function<void(std::optional<QJsonObject>, const QString&)> fatto)
{
    const QString c = codice.trimmed();
    if (c.isEmpty()) {
        fatto(std::nullopt, {});
        return;
    }
    QPointer<QObject> vivo(contesto);
    client.leggi(QStringLiteral("articoli?select=%1&codice=eq.%2").arg(campiArticolo, ClientServer::valoreFiltro(c)),
                 [&client, c, vivo, fatto](const RispostaServer& r) {
        if (!vivo)
            return;
        if (!r.ok()) {
            fatto(std::nullopt, r.errore);
            return;
        }
        const auto righe = r.json().array();
        if (!righe.isEmpty()) {
            fatto(righe.first().toObject(), {});
            return;
        }
        // Non è un codice interno: si prova come barcode.
        client.leggi(QStringLiteral("articoli_barcode?select=articoli(%1)&barcode=eq.%2")
                         .arg(campiArticolo, ClientServer::valoreFiltro(c)),
                     [vivo, fatto](const RispostaServer& r2) {
            if (!vivo)
                return;
            if (!r2.ok()) {
                fatto(std::nullopt, r2.errore);
                return;
            }
            const auto righe2 = r2.json().array();
            if (righe2.isEmpty())
                fatto(std::nullopt, {});
            else
                fatto(righe2.first().toObject().value("articoli").toObject(), {});
        });
    });
}

void riempiCombo(ClientServer& client, QComboBox* combo, const QString& risorsa,
                 std::function<QString(const QJsonObject&)> testo, const QString& campoId,
                 const QVariant& selezionato, bool conVuoto)
{
    QPointer<QComboBox> c(combo);
    c->clear();
    c->addItem(QStringLiteral("Caricamento…"));
    c->setEnabled(false);
    client.leggi(risorsa, [c, testo, campoId, selezionato, conVuoto](const RispostaServer& r) {
        if (!c)
            return;
        c->clear();
        if (!r.ok()) {
            c->addItem(QStringLiteral("Errore: %1").arg(r.errore));
            return;
        }
        if (conVuoto)
            c->addItem(QStringLiteral("—"), QVariant());
        for (const auto& v : r.json().array()) {
            const auto o = v.toObject();
            c->addItem(testo(o), o.value(campoId).toVariant());
        }
        if (selezionato.isValid()) {
            const int i = c->findData(selezionato);
            if (i >= 0)
                c->setCurrentIndex(i);
        }
        c->setEnabled(true);
    });
}

void mostraErroreServer(QWidget* parent, const QString& titolo, const RispostaServer& r)
{
    QMessageBox::warning(parent, titolo,
                         r.stato == 0 ? QStringLiteral("Server non raggiungibile:\n%1").arg(r.errore)
                                      : r.errore);
}

} // namespace mdr
