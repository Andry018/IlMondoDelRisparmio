#pragma once

#include "impostazioni.h"

#include <QWidget>

class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QPushButton;
class QTableWidget;

namespace mdr {

class ArchivioLocale;
class Sincronizzatore;

// Configurazione della postazione, accesso al server e stato della sincronizzazione.
class VistaImpostazioni : public QWidget {
    Q_OBJECT

public:
    VistaImpostazioni(Impostazioni impostazioni, ArchivioLocale& archivio, Sincronizzatore& sync,
                      QString infoPostazione, QWidget* parent = nullptr);

    const Impostazioni& impostazioni() const { return m_imp; }
    void aggiorna();
    void log(const QString& riga);

signals:
    void impostazioniCambiate(const Impostazioni& impostazioni);

private:
    void salva();
    void accedi();
    void esci();

    Impostazioni m_imp;
    ArchivioLocale& m_archivio;
    Sincronizzatore& m_sync;

    QLineEdit* m_terminale = nullptr;
    QLineEdit* m_server = nullptr;
    QLineEdit* m_apiKey = nullptr;
    QLineEdit* m_email = nullptr;
    QLineEdit* m_password = nullptr;
    QPushButton* m_accedi = nullptr;
    QPushButton* m_esci = nullptr;
    QPushButton* m_sincronizza = nullptr;
    QLabel* m_statoAccesso = nullptr;
    QLabel* m_info = nullptr;
    QTableWidget* m_reparti = nullptr;
    QPlainTextEdit* m_log = nullptr;
    QString m_infoPostazione;
};

} // namespace mdr
