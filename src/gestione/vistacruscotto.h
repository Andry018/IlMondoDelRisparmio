#pragma once

#include "vistaserver.h"

#include <QJsonObject>

class QGridLayout;
class QLabel;
class QDateEdit;
class QTableWidget;

namespace mdr {

class GraficoBarre;

// Prima scheda della gestione: com'è andata la giornata e cosa c'è da fare. Una sola chiamata
// al server (rpc cruscotto); i riquadri "Da fare" portano alla scheda giusta.
class VistaCruscotto : public VistaServer {
    Q_OBJECT

public:
    explicit VistaCruscotto(ClientServer& client, QWidget* parent = nullptr);
    void aggiorna() override;
    void mostra(const QJsonObject& dati);   // risposta di cruscotto (anche per i test)
    void mostraServer(const QJsonObject& salute);   // risposta di salute_server

signals:
    // "magazzino", "prezzi", "etichette", "resi", "chiusure", "vendite"
    void apri(const QString& dove);

private:
    QLabel* riquadro(QGridLayout* griglia, int colonna, const QString& titolo, const QString& nome);

    QDateEdit* m_giorno;
    QLabel* m_stato;
    QLabel* m_server;   // backup, prova di ripristino, avvisi
    QLabel* m_incasso;
    QLabel* m_confronto;
    QLabel* m_scontrini;
    QLabel* m_medio;
    QLabel* m_mese;
    GraficoBarre* m_giorni;
    GraficoBarre* m_ore;
    QTableWidget* m_pagamenti;
    QTableWidget* m_articoli;
    QWidget* m_daFare;
    QTableWidget* m_casse;
};

// Grafico a barre semplice (colori dalla palette): etichette sotto, valore sopra la barra.
class GraficoBarre : public QWidget {
    Q_OBJECT

public:
    explicit GraficoBarre(QWidget* parent = nullptr);
    void imposta(const QStringList& etichette, const QList<qint64>& valori, int evidenziata = -1);
    const QList<qint64>& valori() const { return m_valori; }
    QSize minimumSizeHint() const override { return {240, 120}; }

protected:
    void paintEvent(QPaintEvent*) override;

private:
    QStringList m_etichette;
    QList<qint64> m_valori;   // centesimi
    int m_evidenziata = -1;
};

} // namespace mdr
