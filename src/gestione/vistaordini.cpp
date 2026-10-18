#include "vistaordini.h"

#include "comune.h"
#include "core/quantita.h"
#include "dialogoordine.h"
#include "filetabella.h"
#include "tabellaserver.h"

#include <QComboBox>
#include <QDate>
#include <QDateTime>
#include <QFile>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QMessageBox>
#include <QPointer>
#include <QPrintPreviewDialog>
#include <QPrinter>
#include <QPushButton>
#include <QSplitter>
#include <QStandardPaths>
#include <QStyle>
#include <QTextDocument>
#include <QVBoxLayout>

namespace mdr {

namespace {

const QList<QPair<QString, QString>>& modiInvio()
{
    static const QList<QPair<QString, QString>> m{
        {QStringLiteral("whatsapp"), QStringLiteral("WhatsApp")},
        {QStringLiteral("email"), QStringLiteral("Email")},
        {QStringLiteral("agente"), QStringLiteral("All'agente")},
        {QStringLiteral("telefono"), QStringLiteral("Al telefono")},
        {QStringLiteral("di_persona"), QStringLiteral("Comprato di persona")},
        {QStringLiteral("portale"), QStringLiteral("Portale del fornitore")},
        {QStringLiteral("altro"), QStringLiteral("Altro")},
    };
    return m;
}

QString nomeModo(const QString& chiave)
{
    for (const auto& [k, nome] : modiInvio())
        if (k == chiave)
            return nome;
    return chiave;
}

QString nomeStato(const QString& stato)
{
    if (stato == QLatin1String("bozza"))
        return QStringLiteral("Da mandare");
    if (stato == QLatin1String("inviato"))
        return QStringLiteral("Inviato, in arrivo");
    if (stato == QLatin1String("chiuso"))
        return QStringLiteral("Chiuso");
    if (stato == QLatin1String("annullato"))
        return QStringLiteral("Annullato");
    return stato;
}

QString q(const QJsonObject& r, const char* campo)
{
    return Quantita::fromMillesimi(r.value(QLatin1String(campo)).toInteger()).toString();
}

} // namespace

VistaOrdini::VistaOrdini(ClientServer& client, QWidget* parent)
    : VistaServer(client, parent)
{
    auto* layout = new QVBoxLayout(this);
    auto* barra = new QHBoxLayout;
    auto* nuovo = new QPushButton(style()->standardIcon(QStyle::SP_FileDialogNewFolder), QStringLiteral("Nuovo dalla proposta…"), this);
    nuovo->setObjectName("ordini_nuovo");
    nuovo->setToolTip(QStringLiteral("Proposta d'ordine dalle vendite: si correggono le quantità e si creano le bozze"));
    m_filtro = new QComboBox(this);
    m_filtro->setObjectName("ordini_filtro");
    m_filtro->addItem(QStringLiteral("Aperti (da mandare e in arrivo)"), QStringLiteral("stato=in.(bozza,inviato)"));
    m_filtro->addItem(QStringLiteral("Da mandare"), QStringLiteral("stato=eq.bozza"));
    m_filtro->addItem(QStringLiteral("In arrivo"), QStringLiteral("stato=eq.inviato"));
    m_filtro->addItem(QStringLiteral("Tutti (ultimi 200)"), QString());
    auto* aggiorna = new QPushButton(style()->standardIcon(QStyle::SP_BrowserReload), QStringLiteral("Aggiorna"), this);
    barra->addWidget(nuovo);
    barra->addWidget(m_filtro);
    barra->addStretch();
    barra->addWidget(aggiorna);
    layout->addLayout(barra);

    auto* divisore = new QSplitter(Qt::Vertical, this);
    m_ordini = new TabellaServer({
        {"numero", QStringLiteral("N."), ColonnaServer::Intero},
        {"fornitore", QStringLiteral("Fornitore")},
        {"stato", QStringLiteral("Stato"), ColonnaServer::Testo, [](const QJsonObject& o) { return nomeStato(o.value("stato").toString()); }},
        {"modo_invio", QStringLiteral("Mandato"), ColonnaServer::Testo,
         [](const QJsonObject& o) { return o.value("modo_invio").isNull() ? QString() : nomeModo(o.value("modo_invio").toString()); }},
        {"creato_at", QStringLiteral("Creato"), ColonnaServer::DataOra},
        {"inviato_at", QStringLiteral("Inviato"), ColonnaServer::DataOra},
        {"righe", QStringLiteral("Righe"), ColonnaServer::Testo,
         [](const QJsonObject& o) {
             return o.value("stato").toString() == QLatin1String("inviato")
                        ? QStringLiteral("%1 (%2 arrivate)").arg(o.value("righe").toInt()).arg(o.value("righe_arrivate").toInt())
                        : QString::number(o.value("righe").toInt());
         }},
        {"valore", QStringLiteral("Valore €"), ColonnaServer::Euro},
        {"note", QStringLiteral("Note")},
    }, divisore);
    m_ordini->setObjectName("ordini_elenco");
    m_ordini->testoVuoto = QStringLiteral("Nessun ordine: «Nuovo dalla proposta…» per prepararne uno");
    m_ordini->evidenzia = [](const QJsonObject& o) { return o.value("stato").toString() == QLatin1String("bozza"); };

    auto* dettaglio = new QWidget(divisore);
    auto* ld = new QVBoxLayout(dettaglio);
    ld->setContentsMargins(0, 0, 0, 0);
    m_intestazione = new QLabel(dettaglio);
    m_intestazione->setObjectName("ordini_intestazione");
    ld->addWidget(m_intestazione);
    m_righe = new TabellaServer({
        {"codice", QStringLiteral("Codice")},
        {"codice_fornitore", QStringLiteral("Cod. fornitore")},
        {"descrizione", QStringLiteral("Descrizione")},
        {"quantita", QStringLiteral("Ordinato"), ColonnaServer::Quantita},
        {"arrivato", QStringLiteral("Arrivato"), ColonnaServer::Quantita},
        {"mancante", QStringLiteral("Manca"), ColonnaServer::Quantita},
        {"costo_previsto", QStringLiteral("Costo €"), ColonnaServer::Euro},
        {"valore", QStringLiteral("Valore €"), ColonnaServer::Euro},
    }, dettaglio);
    m_righe->setObjectName("ordini_righe");
    m_righe->testoVuoto = QStringLiteral("Scegliere un ordine");
    m_righe->evidenzia = [](const QJsonObject& r) { return r.value("mancante").toInteger() > 0 && r.value("arrivato").toInteger() > 0; };
    ld->addWidget(m_righe, 1);

    auto* comandi = new QHBoxLayout;
    m_invia = new QPushButton(style()->standardIcon(QStyle::SP_ArrowRight), QStringLiteral("Segna come inviato…"), dettaglio);
    m_invia->setObjectName("ordini_invia");
    m_invia->setToolTip(QStringLiteral("Dopo averlo mandato (o comprato di persona): la merce diventa «ordinata»"));
    m_chiudi = new QPushButton(style()->standardIcon(QStyle::SP_DialogApplyButton), QStringLiteral("Chiudi (arrivato)"), dettaglio);
    m_chiudi->setToolTip(QStringLiteral("Quello che manca non si aspetta più: torna nella proposta d'ordine"));
    m_annulla = new QPushButton(style()->standardIcon(QStyle::SP_DialogCancelButton), QStringLiteral("Annulla ordine"), dettaglio);
    m_stampa = new QPushButton(style()->standardIcon(QStyle::SP_FileDialogContentsView), QStringLiteral("Stampa…"), dettaglio);
    m_esporta = new QPushButton(style()->standardIcon(QStyle::SP_DialogSaveButton), QStringLiteral("Esporta per Excel…"), dettaglio);
    for (auto* b : {m_invia, m_chiudi, m_annulla})
        comandi->addWidget(b);
    comandi->addStretch();
    comandi->addWidget(m_stampa);
    comandi->addWidget(m_esporta);
    ld->addLayout(comandi);
    divisore->setStretchFactor(0, 2);
    divisore->setStretchFactor(1, 3);
    layout->addWidget(divisore, 1);

    connect(nuovo, &QPushButton::clicked, this, &VistaOrdini::nuovoDallaProposta);
    connect(m_filtro, &QComboBox::currentIndexChanged, this, &VistaOrdini::aggiorna);
    connect(aggiorna, &QPushButton::clicked, this, &VistaOrdini::aggiorna);
    connect(m_ordini, &TabellaServer::selezioneCambiata, this, &VistaOrdini::mostraRighe);
    connect(m_invia, &QPushButton::clicked, this, &VistaOrdini::invia);
    connect(m_chiudi, &QPushButton::clicked, this, [this] { chiudi(false); });
    connect(m_annulla, &QPushButton::clicked, this, [this] { chiudi(true); });
    connect(m_stampa, &QPushButton::clicked, this, &VistaOrdini::stampa);
    connect(m_esporta, &QPushButton::clicked, this, &VistaOrdini::esporta);
    aggiornaPulsanti();
}

void VistaOrdini::aggiorna()
{
    const QString filtro = m_filtro->currentData().toString();
    QPointer<VistaOrdini> io(this);
    m_ordini->carica(m_client,
                     QStringLiteral("v_ordini_fornitori?order=creato_at.desc&limit=200") + (filtro.isEmpty() ? QString() : QLatin1Char('&') + filtro),
                     [io](const QJsonArray&) {
        if (io)
            io->mostraRighe();
    });
    m_client.leggi(QStringLiteral("v_ordini_fornitori?select=id&stato=in.(bozza,inviato)"), [io](const RispostaServer& r) {
        if (io && r.ok())
            emit io->cambiato(int(r.json().array().size()));
    });
}

void VistaOrdini::mostraRighe()
{
    const auto o = m_ordini->selezionata();
    aggiornaPulsanti();
    if (!o) {
        m_intestazione->clear();
        m_righe->mostra({});
        return;
    }
    QString testo = QStringLiteral("<b>Ordine %1 — %2</b> · %3").arg(o->value("numero").toInt())
                        .arg(o->value("fornitore").toString().toHtmlEscaped(), nomeStato(o->value("stato").toString()));
    if (!o->value("modo_invio").isNull())
        testo += QStringLiteral(" (%1)").arg(nomeModo(o->value("modo_invio").toString()));
    if (o->value("stato").toString() == QLatin1String("inviato"))
        testo += QStringLiteral(" · confrontato con i carichi da %1 arrivati dopo l'invio").arg(o->value("fornitore").toString().toHtmlEscaped());
    m_intestazione->setText(testo);
    m_righe->carica(m_client, QStringLiteral("v_ordini_righe?ordine_id=eq.%1&order=descrizione").arg(o->value("id").toString()));
}

void VistaOrdini::aggiornaPulsanti()
{
    const auto o = m_ordini->selezionata();
    const QString stato = o ? o->value("stato").toString() : QString();
    m_invia->setEnabled(stato == QLatin1String("bozza"));
    m_chiudi->setEnabled(stato == QLatin1String("inviato"));
    m_annulla->setEnabled(stato == QLatin1String("bozza") || stato == QLatin1String("inviato"));
    m_stampa->setEnabled(o.has_value());
    m_esporta->setEnabled(o.has_value());
}

void VistaOrdini::nuovoDallaProposta()
{
    DialogoOrdine d(m_client, this);
    connect(&d, &DialogoOrdine::ordiniCreati, this, [this, &d](int n) {
        if (n > 0) {
            d.accept();
            m_filtro->setCurrentIndex(1);   // da mandare
            aggiorna();
        }
    });
    d.exec();
}

void VistaOrdini::invia()
{
    const auto o = m_ordini->selezionata();
    if (!o)
        return;
    QStringList nomi;
    for (const auto& [k, nome] : modiInvio())
        nomi << nome;
    bool ok = false;
    const QString scelto = QInputDialog::getItem(this, QStringLiteral("Ordine %1").arg(o->value("numero").toInt()),
                                                 QStringLiteral("Come è stato mandato a %1?").arg(o->value("fornitore").toString()),
                                                 nomi, 0, false, &ok);
    if (!ok)
        return;
    const QString modo = modiInvio()[nomi.indexOf(scelto)].first;
    QPointer<VistaOrdini> io(this);
    m_client.rpc(QStringLiteral("invia_ordine"), {{"p_id", o->value("id").toString()}, {"p_modo", modo}},
                 [io](const RispostaServer& r) {
        if (!io)
            return;
        if (!r.ok())
            mostraErroreServer(io, QStringLiteral("Ordini"), r);
        io->aggiorna();
    });
}

void VistaOrdini::chiudi(bool annulla)
{
    const auto o = m_ordini->selezionata();
    if (!o)
        return;
    const qint64 manca = o->value("mancante").toInteger();
    const QString domanda = annulla
        ? QStringLiteral("Annullare l'ordine %1 a %2?").arg(o->value("numero").toInt()).arg(o->value("fornitore").toString())
        : manca > 0 ? QStringLiteral("Chiudere l'ordine %1? Manca ancora della merce: non si aspetta più e torna nella proposta d'ordine.")
                          .arg(o->value("numero").toInt())
                    : QStringLiteral("Chiudere l'ordine %1? È arrivato tutto.").arg(o->value("numero").toInt());
    if (QMessageBox::question(this, QStringLiteral("Ordini"), domanda) != QMessageBox::Yes)
        return;
    QPointer<VistaOrdini> io(this);
    m_client.rpc(QStringLiteral("chiudi_ordine"), {{"p_id", o->value("id").toString()}, {"p_annulla", annulla}},
                 [io](const RispostaServer& r) {
        if (!io)
            return;
        if (!r.ok())
            mostraErroreServer(io, QStringLiteral("Ordini"), r);
        io->aggiorna();
    });
}

void VistaOrdini::componi(QTextDocument& documento, const QJsonObject& o, const QJsonArray& righe)
{
    QString html = QStringLiteral("<h2>Ordine n. %1 — %2</h2><p>Mondo del Risparmio, %3</p>")
                       .arg(o.value("numero").toInt())
                       .arg(o.value("fornitore").toString().toHtmlEscaped(), QDate::currentDate().toString("dd/MM/yyyy"));
    if (!o.value("note").toString().isEmpty())
        html += QStringLiteral("<p>%1</p>").arg(o.value("note").toString().toHtmlEscaped());
    html += QStringLiteral("<table border=1 cellspacing=0 cellpadding=4 width=100%><tr><th>Cod. fornitore</th><th>Codice</th>"
                           "<th>Descrizione</th><th>Quantità</th></tr>");
    for (const auto& v : righe) {
        const auto r = v.toObject();
        html += QStringLiteral("<tr><td>%1</td><td>%2</td><td>%3</td><td align=right><b>%4</b></td></tr>")
                    .arg(r.value("codice_fornitore").toString().toHtmlEscaped(), r.value("codice").toString().toHtmlEscaped(),
                         r.value("descrizione").toString().toHtmlEscaped(), q(r, "quantita"));
    }
    html += QStringLiteral("</table><p>%1 articoli.</p>").arg(righe.size());
    documento.setHtml(html);
}

void VistaOrdini::stampa()
{
    const auto o = m_ordini->selezionata();
    if (!o)
        return;
    QPrinter stampante(QPrinter::HighResolution);
    QPrintPreviewDialog d(&stampante, this);
    d.setWindowTitle(QStringLiteral("Ordine %1 — anteprima").arg(o->value("numero").toInt()));
    d.resize(900, 700);
    const QJsonArray righe = m_righe->righe();
    connect(&d, &QPrintPreviewDialog::paintRequested, this, [&](QPrinter* p) {
        QTextDocument documento;
        componi(documento, *o, righe);
        documento.print(p);
    });
    d.exec();
}

void VistaOrdini::esporta()
{
    const auto o = m_ordini->selezionata();
    if (!o)
        return;
    const QString file = QFileDialog::getSaveFileName(
        this, QStringLiteral("Esporta l'ordine"),
        QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation)
            + QStringLiteral("/ordine-%1.csv").arg(o->value("numero").toInt()),
        QStringLiteral("CSV per Excel (*.csv)"));
    if (file.isEmpty())
        return;
    QList<QStringList> righe;
    for (const auto& v : m_righe->righe()) {
        const auto r = v.toObject();
        righe << QStringList{r.value("codice_fornitore").toString(), r.value("codice").toString(),
                             r.value("descrizione").toString(), q(r, "quantita"), q(r, "arrivato"), q(r, "mancante")};
    }
    QFile f(file);
    if (!f.open(QIODevice::WriteOnly)
        || f.write(scriviCsv({QStringLiteral("Codice fornitore"), QStringLiteral("Codice"), QStringLiteral("Descrizione"),
                              QStringLiteral("Quantità"), QStringLiteral("Arrivato"), QStringLiteral("Manca")}, righe)) < 0)
        QMessageBox::warning(this, QStringLiteral("Ordini"), QStringLiteral("File non salvato: %1").arg(f.errorString()));
}

} // namespace mdr
