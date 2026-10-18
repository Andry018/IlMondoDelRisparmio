#pragma once

#include "vistaserver.h"

#include <QJsonArray>
#include <QJsonObject>

class QComboBox;
class QLabel;
class QPushButton;
class QTextDocument;

namespace mdr {

class TabellaServer;

// Ordini ai fornitori (v_ordini_fornitori, v_ordini_righe): bozze dalla proposta d'ordine, invio
// (si annota come: email, WhatsApp, agente, di persona...), confronto con la merce arrivata,
// chiusura. Il programma non manda nulla: l'ordine si stampa, si esporta o si legge al telefono.
class VistaOrdini : public VistaServer {
    Q_OBJECT

public:
    explicit VistaOrdini(ClientServer& client, QWidget* parent = nullptr);
    void aggiorna() override;

    // Testo dell'ordine da stampare o mandare (anche per i test)
    static void componi(QTextDocument& documento, const QJsonObject& ordine, const QJsonArray& righe);

signals:
    void cambiato(int aperti);   // bozze + inviati, per il numero sulla scheda

private:
    void mostraRighe();
    void nuovoDallaProposta();
    void invia();
    void chiudi(bool annulla);
    void stampa();
    void esporta();
    void aggiornaPulsanti();

    QComboBox* m_filtro;
    TabellaServer* m_ordini;
    TabellaServer* m_righe;
    QLabel* m_intestazione;
    QPushButton* m_invia;
    QPushButton* m_chiudi;
    QPushButton* m_annulla;
    QPushButton* m_stampa;
    QPushButton* m_esporta;
};

} // namespace mdr
