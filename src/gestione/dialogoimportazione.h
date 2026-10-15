#pragma once

#include "importatore.h"

#include <QDialog>

#include <memory>

class QComboBox;
class QFormLayout;
class QLabel;
class QPlainTextEdit;
class QProgressBar;
class QPushButton;
class QTableWidget;

namespace mdr {

class ClientServer;

// "Importa da file": articoli, barcode, clienti, fornitori o giacenze da un CSV/Excel.
// Le colonne del file si abbinano da sole ai campi (anche con nomi diversi) e si possono cambiare;
// l'anteprima mostra le prime righe. "Scarica modello" salva un CSV con le intestazioni giuste.
class DialogoImportazione : public QDialog {
    Q_OBJECT

public:
    DialogoImportazione(ClientServer& client, TipoImportazione tipo, QWidget* parent = nullptr);
    ~DialogoImportazione() override;

    bool caricaFile(const QString& percorso);
    void importa();
    QMap<QString, int> colonneScelte() const;
    const RapportoImportazione* rapporto() const { return m_ultimo.get(); }

    bool avvisi = true;   // false nei test: niente finestre di conferma o di esito

signals:
    void importazioneFinita();

private:
    void scegliFile();
    void salvaModello();
    void costruisciCampi();
    void aggiornaAnteprima();
    void finito();
    TipoImportazione tipo() const;

    ClientServer& m_client;
    QComboBox* m_tipo = nullptr;
    QLabel* m_infoFile = nullptr;
    QFormLayout* m_formCampi = nullptr;
    QWidget* m_campi = nullptr;
    QMap<QString, QComboBox*> m_combo;
    QTableWidget* m_anteprima = nullptr;
    QProgressBar* m_avanzamento = nullptr;
    QPlainTextEdit* m_log = nullptr;
    QPushButton* m_importa = nullptr;
    QPushButton* m_chiudi = nullptr;

    QString m_percorso;
    std::optional<TabellaFile> m_file;
    Importatore* m_importatore = nullptr;
    std::unique_ptr<RapportoImportazione> m_ultimo;
};

// Apre la finestra (modale); "dopo" viene chiamata a ogni importazione finita (per aggiornare la scheda).
void importaDaFile(ClientServer& client, TipoImportazione tipo, QWidget* parent, std::function<void()> dopo);

} // namespace mdr
