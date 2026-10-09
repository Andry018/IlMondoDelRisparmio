#include "vistaarticoliserver.h"

#include "comune.h"
#include "core/barcode.h"
#include "core/quantita.h"
#include "tabellaserver.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPointer>
#include <QPushButton>
#include <QRegularExpression>
#include <QSet>
#include <QTimer>
#include <QVBoxLayout>

namespace mdr {

namespace {

const QString campi = QStringLiteral(
    "id,codice,descrizione,descrizione_breve,unita,prezzo_vendita,costo_ultimo,scorta_minima,attivo,"
    "reparto_id,fornitore_abituale_id,reparti(numero_rt,descrizione),articoli_barcode(barcode),"
    "giacenze(quantita),fornitori!fornitore_abituale_id(ragione_sociale)");

QStringList barcodeDi(const QJsonObject& a)
{
    QStringList out;
    for (const auto& b : a.value("articoli_barcode").toArray())
        out << b.toObject().value("barcode").toString();
    out.sort();
    return out;
}

// Valore per i filtri "ilike" di PostgREST, tra virgolette (virgole e parentesi non rompono or=(...)).
QString perOr(const QString& testo)
{
    QString t = testo;
    t.replace(QLatin1Char('\\'), QStringLiteral("\\\\")).replace(QLatin1Char('"'), QStringLiteral("\\\""));
    return QLatin1Char('"') + t + QLatin1Char('"');
}

class DialogoArticoloServer : public QDialog {
public:
    DialogoArticoloServer(ClientServer& client, const QJsonObject& a, QWidget* parent)
        : QDialog(parent)
        , m_client(client)
        , m_articolo(a)
        , m_barcodeIniziali(barcodeDi(a))
    {
        const bool nuovo = a.isEmpty();
        setWindowTitle(nuovo ? QStringLiteral("Nuovo articolo") : QStringLiteral("Modifica articolo"));
        setMinimumWidth(520);
        auto* form = new QFormLayout(this);

        m_codice = new QLineEdit(a.value("codice").toString(), this);
        m_descrizione = new QLineEdit(a.value("descrizione").toString(), this);
        m_breve = new QLineEdit(a.value("descrizione_breve").toString(), this);
        m_breve->setPlaceholderText(QStringLiteral("per lo scontrino (facoltativa, max 40 caratteri)"));
        m_breve->setMaxLength(40);
        m_reparto = new QComboBox(this);
        riempiCombo(client, m_reparto,
                    QStringLiteral("reparti?select=id,numero_rt,descrizione,aliquote_iva(basis_points,natura)&order=numero_rt"),
                    [](const QJsonObject& r) {
                        return QStringLiteral("%1 — %2").arg(r.value("numero_rt").toInt()).arg(r.value("descrizione").toString());
                    },
                    QStringLiteral("id"), a.value("reparto_id").toVariant());
        m_unita = new QComboBox(this);
        for (const auto* u : {"PZ", "KG", "LT", "MT"})
            m_unita->addItem(QLatin1String(u), QLatin1String(u));
        m_unita->setCurrentIndex(std::max(0, m_unita->findData(a.value("unita").toString(QStringLiteral("PZ")))));
        m_prezzo = new QLineEdit(nuovo ? QString() : Money::fromCents(a.value("prezzo_vendita").toInteger()).toString(), this);
        m_prezzo->setPlaceholderText(QStringLiteral("IVA inclusa, es. 1,99"));
        m_costo = new QLineEdit(Money::fromCents(a.value("costo_ultimo").toInteger()).toString(), this);
        m_costo->setToolTip(QStringLiteral("Costo netto dell'ultimo carico: lo aggiorna anche il carico merce"));
        m_scorta = new QLineEdit(Quantita::fromMillesimi(a.value("scorta_minima").toInteger()).toString(), this);
        m_fornitore = new QComboBox(this);
        riempiCombo(client, m_fornitore, QStringLiteral("fornitori?select=id,ragione_sociale&order=ragione_sociale"),
                    [](const QJsonObject& f) { return f.value("ragione_sociale").toString(); },
                    QStringLiteral("id"), a.value("fornitore_abituale_id").toVariant(), true);
        m_barcode = new QLineEdit(m_barcodeIniziali.join(QStringLiteral(", ")), this);
        m_barcode->setPlaceholderText(QStringLiteral("uno o più, separati da virgola"));
        m_attivo = new QCheckBox(QStringLiteral("In vendita"), this);
        m_attivo->setChecked(nuovo || a.value("attivo").toBool());

        form->addRow(QStringLiteral("Codice"), m_codice);
        form->addRow(QStringLiteral("Descrizione"), m_descrizione);
        form->addRow(QStringLiteral("Descrizione breve"), m_breve);
        form->addRow(QStringLiteral("Reparto / IVA"), m_reparto);
        form->addRow(QStringLiteral("Unità"), m_unita);
        form->addRow(QStringLiteral("Prezzo di vendita €"), m_prezzo);
        form->addRow(QStringLiteral("Costo ultimo €"), m_costo);
        form->addRow(QStringLiteral("Scorta minima"), m_scorta);
        form->addRow(QStringLiteral("Fornitore abituale"), m_fornitore);
        form->addRow(QStringLiteral("Barcode"), m_barcode);
        form->addRow(QString(), m_attivo);
        m_bottoni = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel, this);
        form->addRow(m_bottoni);
        connect(m_bottoni, &QDialogButtonBox::accepted, this, &DialogoArticoloServer::salva);
        connect(m_bottoni, &QDialogButtonBox::rejected, this, &QDialog::reject);
        (nuovo ? m_codice : m_descrizione)->setFocus();
    }

private:
    void errore(const QString& testo, QWidget* campo = nullptr)
    {
        QMessageBox::warning(this, windowTitle(), testo);
        if (campo)
            campo->setFocus();
        m_bottoni->setEnabled(true);
    }

    void salva()
    {
        const auto prezzo = Money::parse(m_prezzo->text());
        const auto costo = Money::parse(m_costo->text().isEmpty() ? QStringLiteral("0") : m_costo->text());
        const auto scorta = Quantita::parse(m_scorta->text().isEmpty() ? QStringLiteral("0") : m_scorta->text());
        if (m_codice->text().trimmed().isEmpty())
            return errore(QStringLiteral("Il codice è obbligatorio."), m_codice);
        if (m_descrizione->text().trimmed().isEmpty())
            return errore(QStringLiteral("La descrizione è obbligatoria."), m_descrizione);
        if (!m_reparto->currentData().isValid())
            return errore(QStringLiteral("Scegliere il reparto."), m_reparto);
        if (!prezzo || *prezzo < Money())
            return errore(QStringLiteral("Prezzo non valido."), m_prezzo);
        if (!costo || *costo < Money())
            return errore(QStringLiteral("Costo non valido."), m_costo);
        if (!scorta || *scorta < Quantita())
            return errore(QStringLiteral("Scorta minima non valida."), m_scorta);

        m_barcodeNuovi.clear();
        for (const auto& b : m_barcode->text().split(QRegularExpression(QStringLiteral("[,;\\s]+")), Qt::SkipEmptyParts)) {
            const bool sembraEan = b.size() == 8 || b.size() == 12 || b.size() == 13;
            if (sembraEan && b.toLongLong() > 0 && !barcodeValido(b))
                return errore(QStringLiteral("Il barcode %1 ha la cifra di controllo sbagliata.").arg(b), m_barcode);
            if (!m_barcodeNuovi.contains(b))
                m_barcodeNuovi << b;
        }

        QJsonObject campi{
            {"codice", m_codice->text().trimmed()},
            {"descrizione", m_descrizione->text().trimmed()},
            {"descrizione_breve", m_breve->text().trimmed().isEmpty() ? QJsonValue() : QJsonValue(m_breve->text().trimmed())},
            {"reparto_id", m_reparto->currentData().toInt()},
            {"unita", m_unita->currentData().toString()},
            {"prezzo_vendita", qint64(prezzo->cents())},
            {"costo_ultimo", qint64(costo->cents())},
            {"scorta_minima", qint64(scorta->millesimi())},
            {"fornitore_abituale_id", m_fornitore->currentData().isValid() ? QJsonValue(m_fornitore->currentData().toString())
                                                                           : QJsonValue()},
            {"attivo", m_attivo->isChecked()},
        };
        m_bottoni->setEnabled(false);
        QPointer<DialogoArticoloServer> io(this);
        auto dopo = [io](const RispostaServer& r) {
            if (!io)
                return;
            if (!r.ok())
                return io->errore(r.errore);
            const auto righe = r.json().array();
            if (righe.isEmpty())
                return io->errore(QStringLiteral("Il server non ha salvato l'articolo (permesso negato)."));
            io->salvaBarcode(righe.first().toObject().value("id").toString());
        };
        if (m_articolo.isEmpty())
            m_client.inserisci(QStringLiteral("articoli"), campi, dopo);
        else
            m_client.modifica(QStringLiteral("articoli"), QStringLiteral("id=eq.") + m_articolo.value("id").toString(),
                              campi, dopo);
    }

    // Barcode tolti e aggiunti rispetto a quelli che c'erano.
    void salvaBarcode(const QString& id)
    {
        // Da qui l'articolo esiste: un nuovo "Salva" (es. dopo un barcode rifiutato) è una modifica.
        m_articolo.insert("id", id);
        QStringList tolti, aggiunti;
        for (const auto& b : m_barcodeIniziali)
            if (!m_barcodeNuovi.contains(b))
                tolti << b;
        for (const auto& b : m_barcodeNuovi)
            if (!m_barcodeIniziali.contains(b))
                aggiunti << b;

        QPointer<DialogoArticoloServer> io(this);
        auto aggiungi = [io, id, aggiunti] {
            if (!io)
                return;
            if (aggiunti.isEmpty()) {
                io->accept();
                return;
            }
            QJsonArray righe;
            for (const auto& b : aggiunti)
                righe.append(QJsonObject{{"barcode", b}, {"articolo_id", id}});
            io->m_client.richiesta("POST", QStringLiteral("/rest/v1/articoli_barcode"),
                                   QJsonDocument(righe).toJson(QJsonDocument::Compact), [io](const RispostaServer& r) {
                if (!io)
                    return;
                if (!r.ok()) {
                    return io->errore(r.codice == QLatin1String("23505")
                                          ? QStringLiteral("Articolo salvato, ma un barcode è già di un altro articolo.")
                                          : QStringLiteral("Articolo salvato, barcode no: %1").arg(r.errore));
                }
                io->accept();
            });
        };
        if (tolti.isEmpty()) {
            aggiungi();
            return;
        }
        QStringList quotati;
        for (const auto& b : tolti)
            quotati << perOr(b);
        m_client.richiesta("DELETE", QStringLiteral("/rest/v1/articoli_barcode?articolo_id=eq.%1&barcode=in.(%2)")
                                         .arg(id, ClientServer::valoreFiltro(quotati.join(QLatin1Char(',')))),
                           {}, [io, aggiungi](const RispostaServer& r) {
            if (!io)
                return;
            if (!r.ok())
                return io->errore(QStringLiteral("Barcode non tolti: %1").arg(r.errore));
            aggiungi();
        });
    }

    ClientServer& m_client;
    QJsonObject m_articolo;
    QStringList m_barcodeIniziali;
    QStringList m_barcodeNuovi;
    QLineEdit* m_codice;
    QLineEdit* m_descrizione;
    QLineEdit* m_breve;
    QComboBox* m_reparto;
    QComboBox* m_unita;
    QLineEdit* m_prezzo;
    QLineEdit* m_costo;
    QLineEdit* m_scorta;
    QComboBox* m_fornitore;
    QLineEdit* m_barcode;
    QCheckBox* m_attivo;
    QDialogButtonBox* m_bottoni;
};

} // namespace

VistaArticoliServer::VistaArticoliServer(ClientServer& client, QWidget* parent)
    : VistaServer(client, parent)
{
    auto* layout = new QVBoxLayout(this);
    auto* barra = new QHBoxLayout;
    m_cerca = new QLineEdit(this);
    m_cerca->setPlaceholderText(QStringLiteral("Cerca per codice, descrizione o barcode"));
    m_cerca->setClearButtonEnabled(true);
    barra->addWidget(m_cerca, 1);
    m_soloAttivi = new QCheckBox(QStringLiteral("Solo in vendita"), this);
    m_soloAttivi->setChecked(true);
    barra->addWidget(m_soloAttivi);
    auto* nuovo = new QPushButton(QStringLiteral("Nuovo articolo"), this);
    nuovo->setObjectName("nuovo_articolo");
    m_modifica = new QPushButton(QStringLiteral("Modifica"), this);
    m_attivo = new QPushButton(QStringLiteral("Disattiva"), this);
    for (auto* b : {nuovo, m_modifica, m_attivo})
        barra->addWidget(b);
    layout->addLayout(barra);

    m_tabella = new TabellaServer({
        {"codice", QStringLiteral("Codice")},
        {"descrizione", QStringLiteral("Descrizione")},
        {"reparti.numero_rt", QStringLiteral("Rep."), ColonnaServer::Intero},
        {"unita", QStringLiteral("Unità")},
        {"prezzo_vendita", QStringLiteral("Prezzo €"), ColonnaServer::Euro},
        {"costo_ultimo", QStringLiteral("Costo €"), ColonnaServer::Euro},
        {"ricarico", QStringLiteral("Ricarico"), ColonnaServer::Testo,
         [](const QJsonObject& a) {
             // Sul netto: il prezzo è IVA inclusa, il costo no. Indicativo (IVA del reparto non scorporata).
             const qint64 costo = a.value("costo_ultimo").toInteger();
             if (costo <= 0)
                 return QString();
             const double r = (a.value("prezzo_vendita").toInteger() - costo) * 100.0 / costo;
             return QStringLiteral("%1%").arg(r, 0, 'f', 0);
         }},
        {"giacenze.quantita", QStringLiteral("Giacenza"), ColonnaServer::Quantita},
        {"scorta_minima", QStringLiteral("Scorta min."), ColonnaServer::Quantita},
        {"fornitori.ragione_sociale", QStringLiteral("Fornitore")},
        {"barcode", QStringLiteral("Barcode"), ColonnaServer::Testo,
         [](const QJsonObject& a) { return barcodeDi(a).join(QStringLiteral(", ")); }},
        {"attivo", QStringLiteral("Stato"), ColonnaServer::Testo,
         [](const QJsonObject& a) { return a.value("attivo").toBool() ? QStringLiteral("In vendita") : QStringLiteral("Disattivato"); }},
    }, this);
    m_tabella->setObjectName("articoli_server");
    m_tabella->testoVuoto = QStringLiteral("Nessun articolo trovato");
    m_tabella->evidenzia = [](const QJsonObject& a) { return !a.value("attivo").toBool(); };
    layout->addWidget(m_tabella, 1);

    // La ricerca parte mezzo secondo dopo l'ultimo tasto.
    auto* ritardo = new QTimer(this);
    ritardo->setSingleShot(true);
    ritardo->setInterval(400);
    connect(m_cerca, &QLineEdit::textChanged, ritardo, qOverload<>(&QTimer::start));
    connect(ritardo, &QTimer::timeout, this, &VistaArticoliServer::aggiorna);
    connect(m_soloAttivi, &QCheckBox::toggled, this, &VistaArticoliServer::aggiorna);
    connect(nuovo, &QPushButton::clicked, this, [this] { modifica({}); });
    connect(m_modifica, &QPushButton::clicked, this, [this] {
        if (const auto a = m_tabella->selezionata())
            modifica(*a);
    });
    connect(m_tabella, &TabellaServer::attivata, this, &VistaArticoliServer::modifica);
    connect(m_attivo, &QPushButton::clicked, this, &VistaArticoliServer::cambiaAttivo);
    connect(m_tabella, &TabellaServer::selezioneCambiata, this, [this] {
        const auto a = m_tabella->selezionata();
        m_modifica->setEnabled(a.has_value());
        m_attivo->setEnabled(a.has_value());
        m_attivo->setText(a && !a->value("attivo").toBool() ? QStringLiteral("Riattiva") : QStringLiteral("Disattiva"));
    });
    m_modifica->setEnabled(false);
    m_attivo->setEnabled(false);
}

void VistaArticoliServer::aggiorna()
{
    const QString testo = m_cerca->text().trimmed();
    QString risorsa = QStringLiteral("articoli?select=%1&order=descrizione&limit=500").arg(campi);
    if (m_soloAttivi->isChecked())
        risorsa += QStringLiteral("&attivo=is.true");
    if (testo.isEmpty()) {
        m_tabella->carica(m_client, risorsa);
        return;
    }
    const QString filtro = QStringLiteral("(codice.ilike.%1,descrizione.ilike.%2)")
                               .arg(perOr(QLatin1Char('*') + testo + QLatin1Char('*')),
                                    perOr(QLatin1Char('*') + testo + QLatin1Char('*')));
    const bool forseBarcode = testo.size() >= 8 && testo.toLongLong() > 0;
    if (!forseBarcode) {
        m_tabella->carica(m_client, risorsa + QStringLiteral("&or=") + ClientServer::valoreFiltro(filtro));
        return;
    }
    // Un barcode: prima si cerca l'articolo che lo possiede.
    QPointer<VistaArticoliServer> io(this);
    m_client.leggi(QStringLiteral("articoli_barcode?select=articolo_id&barcode=eq.") + ClientServer::valoreFiltro(testo),
                   [io, risorsa, filtro](const RispostaServer& r) {
        if (!io)
            return;
        const auto righe = r.json().array();
        if (r.ok() && !righe.isEmpty())
            io->m_tabella->carica(io->m_client, risorsa + QStringLiteral("&id=eq.")
                                                     + righe.first().toObject().value("articolo_id").toString());
        else
            io->m_tabella->carica(io->m_client, risorsa + QStringLiteral("&or=") + ClientServer::valoreFiltro(filtro));
    });
}

void VistaArticoliServer::modifica(const QJsonObject& articolo)
{
    DialogoArticoloServer d(m_client, articolo, this);
    if (d.exec() == QDialog::Accepted)
        aggiorna();
}

void VistaArticoliServer::cambiaAttivo()
{
    const auto a = m_tabella->selezionata();
    if (!a)
        return;
    const bool attivo = !a->value("attivo").toBool();
    QPointer<VistaArticoliServer> io(this);
    m_client.modifica(QStringLiteral("articoli"), QStringLiteral("id=eq.") + a->value("id").toString(),
                      {{"attivo", attivo}}, [io](const RispostaServer& r) {
        if (!io)
            return;
        if (!r.ok() || r.json().array().isEmpty())
            mostraErroreServer(io, QStringLiteral("Articoli"),
                               r.ok() ? RispostaServer{403, {}, QStringLiteral("Permesso negato")} : r);
        io->aggiorna();
    });
}

} // namespace mdr
