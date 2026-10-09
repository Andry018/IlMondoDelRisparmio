#pragma once

#include "locale/archiviolocale.h"

#include <QDialog>
#include <optional>

class QLabel;
class QLineEdit;

namespace mdr {

class OperazioniCassa;

// Identificazione dell'operatore alla cassa: codice e PIN con il tastierino, verificati sulla
// copia locale di v_operatori_cassa (funziona anche senza rete).
class DialogoOperatore : public QDialog {
    Q_OBJECT

public:
    DialogoOperatore(ArchivioLocale& archivio, QWidget* parent = nullptr);

    std::optional<OperatoreCassa> operatore() const { return m_operatore; }

    // Per i test e l'automazione.
    bool prova(const QString& codice, const QString& pin);

private:
    void tasto(const QString& t);
    void conferma();

    ArchivioLocale& m_archivio;
    QLineEdit* m_codice = nullptr;
    QLineEdit* m_pin = nullptr;
    QLineEdit* m_attivo = nullptr;   // campo in cui scrive il tastierino
    QLabel* m_errore = nullptr;
    std::optional<OperatoreCassa> m_operatore;
};

// Se la cassa ha operatori (sincronizzati dal server) e nessuno è identificato, chiede codice e
// PIN. true = si può procedere (operatore impostato, o cassa senza operatori).
bool assicuraOperatore(QWidget* parent, ArchivioLocale& archivio, OperazioniCassa& operazioni);

} // namespace mdr
