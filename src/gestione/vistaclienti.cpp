#include "vistaclienti.h"

#include "tabellaserver.h"

#include <QCheckBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPointer>
#include <QPushButton>
#include <QRegularExpression>
#include <QTimer>
#include <QVBoxLayout>

namespace mdr {

namespace {

// Campi del cliente modificabili qui: (colonna, etichetta).
const QList<QPair<QString, QString>> campiCliente = {
    {"codice", "Codice"}, {"ragione_sociale", "Ragione sociale"}, {"cognome", "Cognome"}, {"nome", "Nome"},
    {"partita_iva", "Partita IVA"}, {"codice_fiscale", "Codice fiscale"}, {"indirizzo", "Indirizzo"},
    {"cap", "CAP"}, {"comune", "Comune"}, {"provincia", "Provincia"}, {"nazione", "Nazione"},
    {"codice_destinatario", "Codice destinatario SDI"}, {"pec", "PEC"}, {"email", "Email"},
    {"telefono", "Telefono"}, {"tessera", "Tessera fedeltà"}, {"note", "Note"},
};

class DialogoCliente : public QDialog {
public:
    DialogoCliente(ClientServer& client, const QJsonObject& c, QWidget* parent)
        : QDialog(parent)
        , m_client(client)
        , m_cliente(c)
    {
        setWindowTitle(c.isEmpty() ? QStringLiteral("Nuovo cliente") : QStringLiteral("Modifica cliente"));
        setMinimumWidth(520);
        auto* form = new QFormLayout(this);
        auto* nota = new QLabel(QStringLiteral("Per la fattura servono: ragione sociale oppure nome e cognome, "
                                               "partita IVA o codice fiscale, indirizzo, CAP e comune. Senza codice "
                                               "destinatario né PEC la fattura va nel cassetto fiscale del cliente."), this);
        nota->setWordWrap(true);
        form->addRow(nota);
        for (const auto& [campo, etichetta] : campiCliente) {
            auto* e = new QLineEdit(c.value(campo).toString(), this);
            m_campi.insert(campo, e);
            form->addRow(etichetta, e);
        }
        if (m_campi["nazione"]->text().isEmpty())
            m_campi["nazione"]->setText(QStringLiteral("IT"));
        m_campi["nazione"]->setMaxLength(2);
        m_campi["provincia"]->setMaxLength(2);
        m_campi["codice_destinatario"]->setMaxLength(7);
        m_attivo = new QCheckBox(QStringLiteral("Attivo"), this);
        m_attivo->setChecked(c.isEmpty() || c.value("attivo").toBool());
        form->addRow(QString(), m_attivo);
        m_bottoni = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel, this);
        form->addRow(m_bottoni);
        connect(m_bottoni, &QDialogButtonBox::accepted, this, &DialogoCliente::salva);
        connect(m_bottoni, &QDialogButtonBox::rejected, this, &QDialog::reject);
    }

private:
    QString valore(const QString& campo) const { return m_campi.value(campo)->text().trimmed(); }

    void salva()
    {
        auto errore = [this](const QString& testo, const QString& campo) {
            QMessageBox::warning(this, windowTitle(), testo);
            m_campi.value(campo)->setFocus();
        };
        if (valore("ragione_sociale").isEmpty() && valore("cognome").isEmpty())
            return errore(QStringLiteral("Indicare la ragione sociale oppure il cognome."), "ragione_sociale");
        const QString nazione = valore("nazione").toUpper();
        const QString piva = valore("partita_iva").toUpper().remove(QRegularExpression(QStringLiteral("^IT|\\s")));
        if (nazione == QLatin1String("IT") && !piva.isEmpty() && !QRegularExpression(QStringLiteral("^[0-9]{11}$")).match(piva).hasMatch())
            return errore(QStringLiteral("La partita IVA italiana ha 11 cifre."), "partita_iva");
        const QString cf = valore("codice_fiscale").toUpper();
        if (!cf.isEmpty() && !QRegularExpression(QStringLiteral("^([A-Z0-9]{16}|[0-9]{11})$")).match(cf).hasMatch())
            return errore(QStringLiteral("Il codice fiscale ha 16 caratteri (11 cifre per le società)."), "codice_fiscale");
        const QString cap = valore("cap");
        if (nazione == QLatin1String("IT") && !cap.isEmpty() && !QRegularExpression(QStringLiteral("^[0-9]{5}$")).match(cap).hasMatch())
            return errore(QStringLiteral("Il CAP italiano ha 5 cifre."), "cap");
        const QString dest = valore("codice_destinatario").toUpper();
        if (!dest.isEmpty() && dest.size() != 6 && dest.size() != 7)
            return errore(QStringLiteral("Il codice destinatario ha 7 caratteri (6 per la Pubblica Amministrazione)."),
                          "codice_destinatario");

        QJsonObject riga;
        for (const auto& [campo, etichetta] : campiCliente) {
            QString v = valore(campo);
            if (campo == QLatin1String("partita_iva"))
                v = piva;
            else if (campo == QLatin1String("codice_fiscale") || campo == QLatin1String("codice_destinatario")
                     || campo == QLatin1String("provincia") || campo == QLatin1String("nazione"))
                v = v.toUpper();
            if (campo == QLatin1String("nazione") && v.isEmpty())
                v = QStringLiteral("IT");
            riga.insert(campo, v.isEmpty() ? QJsonValue() : QJsonValue(v));
        }
        riga.insert("attivo", m_attivo->isChecked());

        m_bottoni->setEnabled(false);
        QPointer<DialogoCliente> io(this);
        auto dopo = [io](const RispostaServer& r) {
            if (!io)
                return;
            io->m_bottoni->setEnabled(true);
            if (!r.ok() || r.json().array().isEmpty()) {
                mostraErroreServer(io, io->windowTitle(),
                                   r.ok() ? RispostaServer{403, {}, QStringLiteral("Permesso negato")} : r);
                return;
            }
            io->accept();
        };
        if (m_cliente.isEmpty())
            m_client.inserisci(QStringLiteral("clienti"), riga, dopo);
        else
            m_client.modifica(QStringLiteral("clienti"), QStringLiteral("id=eq.") + m_cliente.value("id").toString(),
                              riga, dopo);
    }

    ClientServer& m_client;
    QJsonObject m_cliente;
    QMap<QString, QLineEdit*> m_campi;
    QCheckBox* m_attivo;
    QDialogButtonBox* m_bottoni;
};

} // namespace

QString VistaClienti::nomeCliente(const QJsonObject& c)
{
    const auto rs = c.value("ragione_sociale").toString();
    if (!rs.isEmpty())
        return rs;
    return QStringLiteral("%1 %2").arg(c.value("cognome").toString(), c.value("nome").toString()).trimmed();
}

VistaClienti::VistaClienti(ClientServer& client, QWidget* parent)
    : VistaServer(client, parent)
{
    auto* layout = new QVBoxLayout(this);
    auto* barra = new QHBoxLayout;
    m_cerca = new QLineEdit(this);
    m_cerca->setPlaceholderText(QStringLiteral("Cerca per nome, codice, partita IVA o codice fiscale"));
    m_cerca->setClearButtonEnabled(true);
    barra->addWidget(m_cerca, 1);
    auto* nuovo = new QPushButton(QStringLiteral("Nuovo cliente"), this);
    m_modifica = new QPushButton(QStringLiteral("Modifica"), this);
    m_modifica->setEnabled(false);
    barra->addWidget(nuovo);
    barra->addWidget(m_modifica);
    layout->addLayout(barra);

    m_tabella = new TabellaServer({
        {"codice", QStringLiteral("Codice")},
        {"nome", QStringLiteral("Cliente"), ColonnaServer::Testo, &VistaClienti::nomeCliente},
        {"partita_iva", QStringLiteral("Partita IVA")},
        {"codice_fiscale", QStringLiteral("Codice fiscale")},
        {"comune", QStringLiteral("Comune")},
        {"codice_destinatario", QStringLiteral("Cod. SDI")},
        {"pec", QStringLiteral("PEC")},
        {"attivo", QStringLiteral("Attivo"), ColonnaServer::SiNo},
    }, this);
    m_tabella->setObjectName("clienti");
    m_tabella->testoVuoto = QStringLiteral("Nessun cliente trovato");
    layout->addWidget(m_tabella, 1);

    auto* ritardo = new QTimer(this);
    ritardo->setSingleShot(true);
    ritardo->setInterval(400);
    connect(m_cerca, &QLineEdit::textChanged, ritardo, qOverload<>(&QTimer::start));
    connect(ritardo, &QTimer::timeout, this, &VistaClienti::aggiorna);
    connect(nuovo, &QPushButton::clicked, this, [this] { modifica({}); });
    connect(m_modifica, &QPushButton::clicked, this, [this] {
        if (const auto c = m_tabella->selezionata())
            modifica(*c);
    });
    connect(m_tabella, &TabellaServer::attivata, this, &VistaClienti::modifica);
    connect(m_tabella, &TabellaServer::selezioneCambiata, this,
            [this] { m_modifica->setEnabled(m_tabella->selezionata().has_value()); });
}

void VistaClienti::aggiorna()
{
    QString risorsa = QStringLiteral("clienti?select=*&order=ragione_sociale.nullsfirst,cognome,nome&limit=500");
    const QString t = m_cerca->text().trimmed();
    if (!t.isEmpty()) {
        QString v = QLatin1Char('*') + t + QLatin1Char('*');
        v.replace(QLatin1Char('"'), QStringLiteral("\\\""));
        v = QLatin1Char('"') + v + QLatin1Char('"');
        QStringList parti;
        for (const auto* c : {"ragione_sociale", "cognome", "nome", "codice", "partita_iva", "codice_fiscale"})
            parti << QStringLiteral("%1.ilike.%2").arg(QLatin1String(c), v);
        risorsa += QStringLiteral("&or=") + ClientServer::valoreFiltro(QLatin1Char('(') + parti.join(QLatin1Char(',')) + QLatin1Char(')'));
    }
    m_tabella->carica(m_client, risorsa);
}

void VistaClienti::modifica(const QJsonObject& cliente)
{
    DialogoCliente d(m_client, cliente, this);
    if (d.exec() == QDialog::Accepted)
        aggiorna();
}

} // namespace mdr
