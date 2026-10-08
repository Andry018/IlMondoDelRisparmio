#pragma once

#include "impostazioni.h"
#include "locale/operazionicassa.h"

#include <QMainWindow>

class QLabel;
class QTabWidget;
class QTimer;

namespace mdr {

class Sincronizzatore;
class VistaArticoli;
class VistaCassa;
class VistaImpostazioni;
class VistaVendite;

class FinestraPrincipale : public QMainWindow {
    Q_OBJECT

public:
    FinestraPrincipale(ArchivioLocale& archivio, OperazioniCassa& operazioni, Impostazioni impostazioni,
                       QString fileImpostazioni, QString infoPostazione, QWidget* parent = nullptr);

    VistaCassa* cassa() const { return m_cassa; }
    VistaVendite* vendite() const { return m_vendite; }

protected:
    void closeEvent(QCloseEvent* e) override;

private:
    void applica(const Impostazioni& imp);
    void cambioScheda(int indice);
    void aggiornaStato();
    void sincronizzaPresto();

    ArchivioLocale& m_archivio;
    OperazioniCassa& m_op;
    QString m_fileImpostazioni;
    Sincronizzatore* m_sync = nullptr;

    QTabWidget* m_schede = nullptr;
    VistaCassa* m_cassa = nullptr;
    VistaVendite* m_vendite = nullptr;
    VistaArticoli* m_articoli = nullptr;
    VistaImpostazioni* m_impostazioni = nullptr;

    QLabel* m_statoTerminale = nullptr;
    QLabel* m_statoCoda = nullptr;
    QLabel* m_statoServer = nullptr;
    QTimer* m_periodico = nullptr;
    QTimer* m_dopoVendita = nullptr;
};

} // namespace mdr
