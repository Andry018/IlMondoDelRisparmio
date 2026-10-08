#pragma once

#include "locale/archiviolocale.h"

#include <QWidget>

class QLabel;
class QLineEdit;
class QPushButton;
class QTableWidget;

namespace mdr {

// Anagrafica articoli dell'archivio locale. Con un server configurato gli articoli arrivano
// dalla sincronizzazione e qui si possono solo consultare.
class VistaArticoli : public QWidget {
    Q_OBJECT

public:
    explicit VistaArticoli(ArchivioLocale& archivio, QWidget* parent = nullptr);

    void impostaSoloLettura(bool soloLettura);
    void aggiorna();

private:
    void modifica(std::optional<Articolo> articolo);
    void cambiaAttivo();
    std::optional<Articolo> selezionato() const;

    ArchivioLocale& m_archivio;
    QList<Articolo> m_articoli;
    bool m_soloLettura = false;

    QLineEdit* m_cerca = nullptr;
    QTableWidget* m_tabella = nullptr;
    QLabel* m_avviso = nullptr;
    QPushButton* m_nuovo = nullptr;
    QPushButton* m_modifica = nullptr;
    QPushButton* m_attivo = nullptr;
};

} // namespace mdr
