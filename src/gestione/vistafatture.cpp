#include "vistafatture.h"

#include "comune.h"
#include "core/quantita.h"
#include "tabellaserver.h"
#include "vistaclienti.h"

#include <QButtonGroup>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFile>
#include <QFileDialog>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QJsonArray>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPointer>
#include <QPushButton>
#include <QRadioButton>
#include <QStackedWidget>
#include <QTableWidget>
#include <QUuid>
#include <QVBoxLayout>

namespace mdr {

namespace {

QString nomeStato(const QString& s)
{
    static const QMap<QString, QString> nomi = {
        {"da_inviare", "Da inviare"}, {"invio_in_corso", "Invio in corso"}, {"inviata", "Inviata"},
        {"consegnata", "Consegnata"}, {"scartata", "SCARTATA"}, {"errore", "ERRORE"}};
    return nomi.value(s, s);
}

bool daCorreggere(const QJsonObject& f)
{
    const auto s = f.value("stato").toString();
    return s == QLatin1String("errore") || s == QLatin1String("scartata");
}

// Emissione di una fattura (da scontrino o con righe) o di una nota di credito.
class DialogoFattura : public QDialog {
public:
    // origine: per la nota di credito, la fattura che si storna.
    DialogoFattura(ClientServer& client, const QJsonObject& origine, QWidget* parent)
        : QDialog(parent)
        , m_client(client)
        , m_origine(origine)
        , m_id(QUuid::createUuid().toString(QUuid::WithoutBraces))   // stesso id se si riprova
    {
        const bool nota = !origine.isEmpty();
        setWindowTitle(nota ? QStringLiteral("Nota di credito sulla fattura %1").arg(origine.value("numero_documento").toString())
                            : QStringLiteral("Nuova fattura"));
        resize(860, 620);
        auto* layout = new QVBoxLayout(this);
        auto* form = new QFormLayout;
        m_cliente = new QComboBox(this);
        riempiCombo(client, m_cliente,
                    QStringLiteral("clienti?select=id,ragione_sociale,cognome,nome&attivo=is.true&order=ragione_sociale.nullsfirst,cognome"),
                    &VistaClienti::nomeCliente, QStringLiteral("id"), origine.value("cliente_id").toVariant());
        m_cliente->setEnabled(!nota);
        m_pagamento = new QComboBox(this);
        m_pagamento->addItem(nota ? QStringLiteral("—") : QStringLiteral("Dal pagamento dello scontrino"), QVariant());
        for (const auto& [codice, nome] : QList<QPair<QString, QString>>{
                 {"MP01", "Contanti"}, {"MP02", "Assegno"}, {"MP05", "Bonifico"}, {"MP08", "Carta di pagamento"}})
            m_pagamento->addItem(QStringLiteral("%1 — %2").arg(codice, nome), codice);
        m_causale = new QLineEdit(this);
        form->addRow(QStringLiteral("Cliente"), m_cliente);
        form->addRow(QStringLiteral("Pagamento"), m_pagamento);
        form->addRow(QStringLiteral("Causale"), m_causale);
        layout->addLayout(form);

        auto* scelta = new QHBoxLayout;
        m_daScontrino = new QRadioButton(QStringLiteral("Fattura di uno scontrino"), this);
        auto* conRighe = new QRadioButton(QStringLiteral("Righe a mano"), this);
        scelta->addWidget(m_daScontrino);
        scelta->addWidget(conRighe);
        scelta->addStretch();
        layout->addLayout(scelta);

        m_pagine = new QStackedWidget(this);
        m_vendite = new TabellaServer({
            {"data_ora", QStringLiteral("Data"), ColonnaServer::DataOra},
            {"terminale_id", QStringLiteral("Cassa")},
            {"documento", QStringLiteral("Scontrino"), ColonnaServer::Testo,
             [](const QJsonObject& v) {
                 return QStringLiteral("%1-%2").arg(v.value("rt_azzeramento").toInt(), 4, 10, QChar('0'))
                     .arg(v.value("rt_numero").toInt(), 4, 10, QChar('0'));
             }},
            {"totale", QStringLiteral("Totale €"), ColonnaServer::Euro},
        }, m_pagine);
        m_vendite->testoVuoto = QStringLiteral("Nessuna vendita recente");
        m_pagine->addWidget(m_vendite);

        auto* paginaRighe = new QWidget(m_pagine);
        auto* lr = new QVBoxLayout(paginaRighe);
        lr->setContentsMargins(0, 0, 0, 0);
        m_righe = new QTableWidget(0, 4, paginaRighe);
        m_righe->setHorizontalHeaderLabels({QStringLiteral("Descrizione"), QStringLiteral("Quantità"),
                                            QStringLiteral("Prezzo IVA inclusa €"), QStringLiteral("IVA")});
        m_righe->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
        m_righe->verticalHeader()->setVisible(false);
        lr->addWidget(m_righe, 1);
        auto* br = new QHBoxLayout;
        auto* piu = new QPushButton(QStringLiteral("Aggiungi riga"), paginaRighe);
        auto* meno = new QPushButton(QStringLiteral("Togli riga"), paginaRighe);
        br->addWidget(piu);
        br->addWidget(meno);
        br->addStretch();
        lr->addLayout(br);
        m_pagine->addWidget(paginaRighe);
        layout->addWidget(m_pagine, 1);

        auto* bottoni = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
        m_ok = bottoni->button(QDialogButtonBox::Ok);
        m_ok->setText(nota ? QStringLiteral("Emetti nota di credito") : QStringLiteral("Emetti fattura"));
        layout->addWidget(bottoni);

        connect(m_daScontrino, &QRadioButton::toggled, this, [this](bool s) { m_pagine->setCurrentIndex(s ? 0 : 1); });
        connect(piu, &QPushButton::clicked, this, &DialogoFattura::aggiungiRiga);
        connect(meno, &QPushButton::clicked, this, [this] {
            if (m_righe->currentRow() >= 0)
                m_righe->removeRow(m_righe->currentRow());
        });
        connect(bottoni, &QDialogButtonBox::accepted, this, &DialogoFattura::emetti);
        connect(bottoni, &QDialogButtonBox::rejected, this, &QDialog::reject);

        // Aliquote per le righe a mano.
        QPointer<DialogoFattura> io(this);
        client.leggi(QStringLiteral("aliquote_iva?select=basis_points,natura,descrizione&attiva=is.true&order=basis_points.desc"),
                     [io](const RispostaServer& r) {
            if (!io)
                return;
            for (const auto& v : r.json().array())
                io->m_aliquote << v.toObject();
            if (io->m_righe->rowCount() == 0)
                io->aggiungiRiga();
        });

        if (nota) {
            // La nota di credito si emette con righe (docs/API.md).
            m_daScontrino->setEnabled(false);
            conRighe->setChecked(true);
            m_pagine->setCurrentIndex(1);
        } else {
            m_daScontrino->setChecked(true);
            m_vendite->carica(client, QStringLiteral("vendite?select=id,data_ora,terminale_id,rt_azzeramento,rt_numero,totale"
                                                     "&tipo=eq.vendita&stato=eq.completata&order=data_ora.desc&limit=200"));
        }
    }

private:
    void aggiungiRiga()
    {
        const int r = m_righe->rowCount();
        m_righe->insertRow(r);
        m_righe->setItem(r, 0, new QTableWidgetItem);
        m_righe->setItem(r, 1, new QTableWidgetItem(QStringLiteral("1")));
        m_righe->setItem(r, 2, new QTableWidgetItem);
        auto* iva = new QComboBox(m_righe);
        for (const auto& a : std::as_const(m_aliquote))
            iva->addItem(a.value("descrizione").toString(), a);
        m_righe->setCellWidget(r, 3, iva);
        m_righe->setCurrentCell(r, 0);
    }

    void emetti()
    {
        if (!m_cliente->currentData().isValid()) {
            QMessageBox::warning(this, windowTitle(), QStringLiteral("Scegliere il cliente."));
            return;
        }
        QJsonObject p{{"id", m_id}, {"cliente_id", m_cliente->currentData().toString()}};
        if (!m_origine.isEmpty()) {
            p.insert("tipo", QStringLiteral("nota_credito"));
            p.insert("documento_origine_id", m_origine.value("id").toString());
        }
        if (m_pagamento->currentData().isValid())
            p.insert("modalita_pagamento", m_pagamento->currentData().toString());
        if (!m_causale->text().trimmed().isEmpty())
            p.insert("causale", m_causale->text().trimmed());

        if (m_daScontrino->isChecked()) {
            const auto v = m_vendite->selezionata();
            if (!v) {
                QMessageBox::warning(this, windowTitle(), QStringLiteral("Scegliere lo scontrino da fatturare."));
                return;
            }
            p.insert("vendita_id", v->value("id").toString());
        } else {
            QJsonArray righe;
            for (int r = 0; r < m_righe->rowCount(); ++r) {
                const QString descr = m_righe->item(r, 0)->text().trimmed();
                const auto q = Quantita::parse(m_righe->item(r, 1)->text());
                const auto prezzo = Money::parse(m_righe->item(r, 2)->text());
                if (descr.isEmpty() && m_righe->item(r, 2)->text().trimmed().isEmpty())
                    continue;   // riga vuota
                if (descr.isEmpty() || !q || *q <= Quantita() || !prezzo || *prezzo < Money()) {
                    QMessageBox::warning(this, windowTitle(), QStringLiteral("Riga %1: descrizione, quantità o prezzo non validi.").arg(r + 1));
                    return;
                }
                const auto iva = qobject_cast<QComboBox*>(m_righe->cellWidget(r, 3))->currentData().toJsonObject();
                QJsonObject riga{{"descrizione", descr}, {"quantita", qint64(q->millesimi())},
                                 {"prezzo_unitario", qint64(prezzo->cents())},
                                 {"aliquota_bp", iva.value("basis_points").toInt()}};
                if (!iva.value("natura").isNull())
                    riga.insert("natura", iva.value("natura"));
                righe.append(riga);
            }
            if (righe.isEmpty()) {
                QMessageBox::warning(this, windowTitle(), QStringLiteral("Aggiungere almeno una riga."));
                return;
            }
            p.insert("righe", righe);
        }

        m_ok->setEnabled(false);
        QPointer<DialogoFattura> io(this);
        m_client.rpc(QStringLiteral("emetti_fattura"), {{"p", p}}, [io](const RispostaServer& r) {
            if (!io)
                return;
            io->m_ok->setEnabled(true);
            if (!r.ok()) {
                mostraErroreServer(io, io->windowTitle(), r);
                return;
            }
            io->accept();
        });
    }

    ClientServer& m_client;
    QJsonObject m_origine;
    QString m_id;
    QComboBox* m_cliente;
    QComboBox* m_pagamento;
    QLineEdit* m_causale;
    QRadioButton* m_daScontrino;
    QStackedWidget* m_pagine;
    TabellaServer* m_vendite;
    QTableWidget* m_righe;
    QPushButton* m_ok;
    QList<QJsonObject> m_aliquote;
};

} // namespace

VistaFatture::VistaFatture(ClientServer& client, QWidget* parent)
    : VistaServer(client, parent)
{
    auto* layout = new QVBoxLayout(this);
    auto* barra = new QHBoxLayout;
    m_filtro = new QComboBox(this);
    m_filtro->addItem(QStringLiteral("Tutte"), QString());
    m_filtro->addItem(QStringLiteral("Da correggere (errore o scartate)"), QStringLiteral("stato=in.(errore,scartata)"));
    m_filtro->addItem(QStringLiteral("In attesa (da inviare, in invio, inviate)"),
                      QStringLiteral("stato=in.(da_inviare,invio_in_corso,inviata)"));
    barra->addWidget(m_filtro);
    barra->addStretch();
    auto* nuova = new QPushButton(QStringLiteral("Nuova fattura…"), this);
    nuova->setObjectName("nuova_fattura");
    m_nota = new QPushButton(QStringLiteral("Nota di credito…"), this);
    m_rigenera = new QPushButton(QStringLiteral("Rigenera e reinvia"), this);
    m_xml = new QPushButton(QStringLiteral("Vedi XML"), this);
    auto* aggiorna = new QPushButton(QStringLiteral("Aggiorna"), this);
    for (auto* b : {nuova, m_nota, m_rigenera, m_xml, aggiorna})
        barra->addWidget(b);
    layout->addLayout(barra);

    m_tabella = new TabellaServer({
        {"data", QStringLiteral("Data"), ColonnaServer::Data},
        {"numero_documento", QStringLiteral("Numero")},
        {"tipo", QStringLiteral("Tipo"), ColonnaServer::Testo,
         [](const QJsonObject& f) { return f.value("tipo").toString() == QLatin1String("nota_credito") ? QStringLiteral("Nota di credito") : QStringLiteral("Fattura"); }},
        {"cliente", QStringLiteral("Cliente")},
        {"imponibile", QStringLiteral("Imponibile €"), ColonnaServer::Euro},
        {"imposta", QStringLiteral("IVA €"), ColonnaServer::Euro},
        {"totale", QStringLiteral("Totale €"), ColonnaServer::Euro},
        {"stato", QStringLiteral("Stato SDI"), ColonnaServer::Testo,
         [](const QJsonObject& f) { return nomeStato(f.value("stato").toString()); }},
        {"messaggio", QStringLiteral("Esito / errore"), ColonnaServer::Testo,
         [](const QJsonObject& f) {
             const auto e = f.value("ultimo_errore").toString();
             return e.isEmpty() ? f.value("sdi_messaggio").toString() : e;
         }},
    }, this);
    m_tabella->setObjectName("fatture");
    m_tabella->testoVuoto = QStringLiteral("Nessuna fattura");
    m_tabella->evidenzia = daCorreggere;
    layout->addWidget(m_tabella, 1);
    auto* aiuto = new QLabel(QStringLiteral("Fatture in rosso: correggere i dati (cliente nella scheda Clienti, dati del "
                                            "negozio nella scheda Negozio) e premere «Rigenera e reinvia» entro 5 giorni."), this);
    aiuto->setWordWrap(true);
    layout->addWidget(aiuto);

    connect(m_filtro, &QComboBox::currentIndexChanged, this, &VistaFatture::aggiorna);
    connect(nuova, &QPushButton::clicked, this, &VistaFatture::nuovaFattura);
    connect(m_nota, &QPushButton::clicked, this, &VistaFatture::notaDiCredito);
    connect(m_rigenera, &QPushButton::clicked, this, &VistaFatture::rigenera);
    connect(m_xml, &QPushButton::clicked, this, &VistaFatture::mostraXml);
    connect(aggiorna, &QPushButton::clicked, this, &VistaFatture::aggiorna);
    connect(m_tabella, &TabellaServer::selezioneCambiata, this, &VistaFatture::aggiornaPulsanti);
    connect(m_tabella, &TabellaServer::attivata, this, &VistaFatture::mostraXml);
    aggiornaPulsanti();
}

void VistaFatture::aggiornaPulsanti()
{
    const auto f = m_tabella->selezionata();
    m_nota->setEnabled(f && f->value("tipo").toString() == QLatin1String("fattura"));
    m_rigenera->setEnabled(f && daCorreggere(*f));
    m_xml->setEnabled(f.has_value());
}

void VistaFatture::aggiorna()
{
    QString risorsa = QStringLiteral("v_fatture?order=data.desc,numero.desc&limit=300");
    const auto filtro = m_filtro->currentData().toString();
    if (!filtro.isEmpty())
        risorsa += QLatin1Char('&') + filtro;
    m_tabella->carica(m_client, risorsa, [this](const QJsonArray&) { aggiornaPulsanti(); });
}

void VistaFatture::nuovaFattura()
{
    DialogoFattura d(m_client, {}, this);
    if (d.exec() == QDialog::Accepted)
        aggiorna();
}

void VistaFatture::notaDiCredito()
{
    const auto f = m_tabella->selezionata();
    if (!f)
        return;
    DialogoFattura d(m_client, *f, this);
    if (d.exec() == QDialog::Accepted)
        aggiorna();
}

void VistaFatture::rigenera()
{
    const auto f = m_tabella->selezionata();
    if (!f)
        return;
    QPointer<VistaFatture> io(this);
    m_client.rpc(QStringLiteral("rigenera_fattura_sdi"), {{"p_documento_id", f->value("id").toString()}},
                 [io](const RispostaServer& r) {
        if (!io)
            return;
        if (!r.ok())
            mostraErroreServer(io, QStringLiteral("Rigenera fattura"), r);
        else
            QMessageBox::information(io, QStringLiteral("Rigenera fattura"),
                                     QStringLiteral("Fattura rigenerata e rimessa in coda per lo SDI."));
        io->aggiorna();
    });
}

void VistaFatture::mostraXml()
{
    const auto f = m_tabella->selezionata();
    if (!f)
        return;
    const QString numero = f->value("numero_documento").toString();
    QPointer<VistaFatture> io(this);
    m_client.rpc(QStringLiteral("fattura_xml"), {{"p_documento_id", f->value("id").toString()}},
                 [io, numero](const RispostaServer& r) {
        if (!io)
            return;
        if (!r.ok()) {
            mostraErroreServer(io, QStringLiteral("Fattura"), r);
            return;
        }
        // La RPC restituisce una stringa JSON con l'XML.
        const QString xml = QJsonDocument::fromJson("[" + r.corpo + "]").array().first().toString();
        QDialog d(io);
        d.setWindowTitle(QStringLiteral("XML della fattura %1").arg(numero));
        d.resize(900, 640);
        auto* l = new QVBoxLayout(&d);
        auto* testo = new QPlainTextEdit(xml, &d);
        testo->setReadOnly(true);
        testo->setLineWrapMode(QPlainTextEdit::NoWrap);
        l->addWidget(testo);
        auto* bottoni = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Close, &d);
        l->addWidget(bottoni);
        QObject::connect(bottoni, &QDialogButtonBox::rejected, &d, &QDialog::reject);
        QObject::connect(bottoni, &QDialogButtonBox::accepted, &d, [&d, xml, numero] {
            QString nome = numero;
            nome.replace(QLatin1Char('/'), QLatin1Char('-'));
            const auto file = QFileDialog::getSaveFileName(&d, QStringLiteral("Salva XML"),
                                                           QStringLiteral("fattura-%1.xml").arg(nome),
                                                           QStringLiteral("XML (*.xml)"));
            if (file.isEmpty())
                return;
            QFile f(file);
            if (f.open(QIODevice::WriteOnly))
                f.write(xml.toUtf8());
        });
        d.exec();
    });
}

} // namespace mdr
