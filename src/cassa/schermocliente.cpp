#include "schermocliente.h"

#include "comune.h"

#include <QGuiApplication>
#include <QHeaderView>
#include <QLabel>
#include <QScreen>
#include <QTableWidget>
#include <QTimer>
#include <QVBoxLayout>

namespace mdr {

SchermoCliente::SchermoCliente(QWidget* parent)
    : QWidget(parent, Qt::Window | Qt::FramelessWindowHint)
    , m_benvenuto(QStringLiteral("Benvenuti al Mondo del Risparmio"))
{
    setWindowTitle(QStringLiteral("Mondo del Risparmio — Cliente"));
    setObjectName("schermo_cliente");
    // Colori fissi, indipendenti dal tema della cassa: si legge da lontano
    setStyleSheet(QStringLiteral(
        "#schermo_cliente { background: #10263b; }"
        "QLabel { color: white; }"
        "QTableWidget { background: #10263b; color: white; border: none; gridline-color: #10263b; }"
        "QHeaderView::section { background: #10263b; color: #9fb7cc; border: none; font-size: 13pt; padding: 4px; }"));
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(32, 24, 32, 24);
    m_titolo = new QLabel(this);
    m_titolo->setObjectName("cliente_titolo");
    m_titolo->setAlignment(Qt::AlignCenter);
    m_titolo->setWordWrap(true);
    m_titolo->setFont(fontGrande(font(), 26, true));
    layout->addWidget(m_titolo);

    m_righe = new QTableWidget(0, 3, this);
    m_righe->setObjectName("cliente_righe");
    m_righe->setHorizontalHeaderLabels({QStringLiteral("Articolo"), QStringLiteral("Q.tà"), QStringLiteral("Importo")});
    m_righe->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    m_righe->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_righe->verticalHeader()->hide();
    m_righe->setShowGrid(false);
    m_righe->setFocusPolicy(Qt::NoFocus);
    m_righe->setSelectionMode(QAbstractItemView::NoSelection);
    m_righe->setFont(fontGrande(font(), 18));
    m_righe->verticalHeader()->setDefaultSectionSize(40);
    m_righe->horizontalHeader()->setMinimumSectionSize(110);
    layout->addWidget(m_righe, 1);

    m_dettaglio = new QLabel(this);
    m_dettaglio->setObjectName("cliente_dettaglio");
    m_dettaglio->setAlignment(Qt::AlignRight);
    m_dettaglio->setFont(fontGrande(font(), 20));
    layout->addWidget(m_dettaglio);
    m_totale = new QLabel(this);
    m_totale->setObjectName("cliente_totale");
    m_totale->setAlignment(Qt::AlignRight);
    m_totale->setFont(fontGrande(font(), 44, true));
    m_totale->setStyleSheet(QStringLiteral("color: #ffd54f;"));
    layout->addWidget(m_totale);

    m_fine = new QTimer(this);
    m_fine->setSingleShot(true);
    m_fine->setInterval(secondiFine * 1000);
    connect(m_fine, &QTimer::timeout, this, &SchermoCliente::benvenuto);
    benvenuto();
}

void SchermoCliente::impostaMessaggio(const QString& testo)
{
    m_benvenuto = testo.trimmed().isEmpty() ? QStringLiteral("Benvenuti") : testo.trimmed();
    if (m_righe->rowCount() == 0 && !m_fine->isActive())
        benvenuto();
}

// Senza righe la scritta occupa il centro dello schermo; con le righe resta in alto, piccola.
static void centrale(QLabel* titolo, QFont base, bool centro)
{
    titolo->setSizePolicy(QSizePolicy::Preferred, centro ? QSizePolicy::Expanding : QSizePolicy::Maximum);
    titolo->setFont(fontGrande(base, centro ? 40 : 26, true));
}

void SchermoCliente::benvenuto()
{
    centrale(m_titolo, font(), true);
    m_titolo->setText(m_benvenuto);
    m_righe->setRowCount(0);
    m_righe->hide();
    m_dettaglio->clear();
    m_totale->clear();
}

void SchermoCliente::mostra(const Carrello& c)
{
    if (c.vuoto()) {
        if (!m_fine->isActive())
            benvenuto();
        return;
    }
    m_fine->stop();
    centrale(m_titolo, font(), false);
    m_righe->show();
    const auto& righe = c.righe();
    m_righe->setRowCount(int(righe.size()));
    for (int i = 0; i < righe.size(); ++i) {
        const auto& r = righe[i];
        m_righe->setItem(i, 0, cella(r.sconto.isZero() ? r.descrizione
                                                       : QStringLiteral("%1  (sconto %2)").arg(r.descrizione, r.sconto.toString())));
        m_righe->setItem(i, 1, cella(r.quantita.toString(), true));
        m_righe->setItem(i, 2, cella(r.totale().toString(), true));
    }
    m_righe->scrollToBottom();   // l'ultimo articolo letto si vede sempre
    m_titolo->setText(QStringLiteral("%1  %2").arg(righe.last().descrizione, euro(righe.last().prezzoUnitario)));
    m_dettaglio->setText(c.pagamenti().isEmpty() ? QString()
                                                 : QStringLiteral("Pagato %1 — da pagare %2").arg(euro(c.pagato()), euro(c.residuo())));
    m_totale->setText(QStringLiteral("TOTALE %1").arg(euro(c.totale())));
}

void SchermoCliente::mostraFine(Money totale, Money resto)
{
    m_righe->setRowCount(0);
    m_righe->hide();
    centrale(m_titolo, font(), true);
    m_titolo->setText(QStringLiteral("Grazie e arrivederci!"));
    m_dettaglio->setText(QStringLiteral("Totale %1").arg(euro(totale)));
    m_totale->setText(resto > Money() ? QStringLiteral("RESTO %1").arg(euro(resto)) : QString());
    m_fine->start();
}

bool SchermoCliente::apriSulSecondoSchermo()
{
    const auto schermi = QGuiApplication::screens();
    if (schermi.size() < 2) {
        hide();
        return false;
    }
    // Il secondo schermo: quello che non è il principale
    QScreen* cliente = schermi.last() == QGuiApplication::primaryScreen() ? schermi.first() : schermi.last();
    setScreen(cliente);
    setGeometry(cliente->geometry());
    showFullScreen();
    return true;
}

} // namespace mdr
