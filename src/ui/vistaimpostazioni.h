#pragma once

#include "impostazioni.h"

#include <QWidget>

class QComboBox;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QPushButton;
class QTableWidget;

namespace mdr {

class Aggiornatore;
class ArchivioLocale;
class Sincronizzatore;

// Configurazione della postazione, accesso al server e stato della sincronizzazione.
class VistaImpostazioni : public QWidget {
    Q_OBJECT

public:
    VistaImpostazioni(Postazione postazione, Impostazioni impostazioni, ArchivioLocale& archivio,
                      Sincronizzatore& sync, Aggiornatore& aggiornatore, QString infoPostazione,
                      QWidget* parent = nullptr);

    const Impostazioni& impostazioni() const { return m_imp; }
    void aggiorna();
    void log(const QString& riga);

signals:
    void impostazioniCambiate(const Impostazioni& impostazioni);
    void installazioneRichiesta();

private:
    void salva();
    void accedi();
    void esci();
    void aggiornaCampiRt();

    Postazione m_postazione;
    Impostazioni m_imp;
    ArchivioLocale& m_archivio;
    Sincronizzatore& m_sync;
    Aggiornatore& m_aggiornatore;

    QLineEdit* m_terminale = nullptr;     // solo cassa
    QComboBox* m_rtConnessione = nullptr; // solo cassa
    QLineEdit* m_rtPorta = nullptr;
    QLineEdit* m_rtIndirizzo = nullptr;
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
    QLineEdit* m_repository = nullptr;
    QLineEdit* m_tokenAggiornamenti = nullptr;
    QLabel* m_statoAggiornamenti = nullptr;
    QPushButton* m_controlla = nullptr;
    QPushButton* m_installa = nullptr;
    void aggiornaStatoAggiornamenti();
};

} // namespace mdr
