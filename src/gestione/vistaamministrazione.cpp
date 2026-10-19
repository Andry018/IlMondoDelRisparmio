#include "vistaamministrazione.h"

#include "dialogoimportazione.h"

#include "tabellaserver.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QJsonArray>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPointer>
#include <QPushButton>
#include <QRegularExpression>
#include <QStyle>
#include <QTimer>
#include <QVBoxLayout>

namespace mdr {

namespace {

// Risposta di una scrittura con "return=representation": vuota = negata da RLS.
bool scritturaRiuscita(QWidget* parent, const QString& titolo, const RispostaServer& r)
{
    if (r.ok() && !r.json().array().isEmpty())
        return true;
    mostraErroreServer(parent, titolo, r.ok() ? RispostaServer{403, {}, QStringLiteral("Permesso negato")} : r);
    return false;
}

// Dialogo semplice: un campo di testo per colonna.
class DialogoCampi : public QDialog {
public:
    DialogoCampi(const QString& titolo, const QList<QPair<QString, QString>>& campi, const QJsonObject& valori,
                 QWidget* parent)
        : QDialog(parent)
        , m_definizione(campi)
    {
        setWindowTitle(titolo);
        setMinimumWidth(480);
        auto* form = new QFormLayout(this);
        for (const auto& [campo, etichetta] : campi) {
            auto* e = new QLineEdit(valori.value(campo).toVariant().toString(), this);
            m_campi.insert(campo, e);
            form->addRow(etichetta, e);
        }
        m_attivo = new QCheckBox(QStringLiteral("Attivo"), this);
        m_attivo->setChecked(valori.isEmpty() || valori.value("attivo").toBool());
        form->addRow(QString(), m_attivo);
        auto* bottoni = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel, this);
        form->addRow(bottoni);
        connect(bottoni, &QDialogButtonBox::accepted, this, &QDialog::accept);
        connect(bottoni, &QDialogButtonBox::rejected, this, &QDialog::reject);
    }

    QJsonObject valori() const
    {
        QJsonObject o;
        for (const auto& [campo, etichetta] : m_definizione) {
            const auto t = m_campi.value(campo)->text().trimmed();
            o.insert(campo, t.isEmpty() ? QJsonValue() : QJsonValue(t));
        }
        o.insert("attivo", m_attivo->isChecked());
        return o;
    }

private:
    QList<QPair<QString, QString>> m_definizione;
    QMap<QString, QLineEdit*> m_campi;
    QCheckBox* m_attivo;
};

const QList<QPair<QString, QString>> campiFornitore = {
    {"codice", "Codice"}, {"ragione_sociale", "Ragione sociale"}, {"partita_iva", "Partita IVA"},
    {"codice_fiscale", "Codice fiscale"}, {"indirizzo", "Indirizzo"}, {"cap", "CAP"}, {"comune", "Comune"},
    {"provincia", "Provincia"}, {"telefono", "Telefono"}, {"email", "Email"}, {"pec", "PEC"}, {"iban", "IBAN"},
    {"giorni_ordine", "Si ordina ogni (giorni)"}, {"note", "Note"},
};

} // namespace

// ---------------------------------------------------------------- Fornitori

VistaFornitori::VistaFornitori(ClientServer& client, QWidget* parent)
    : VistaServer(client, parent)
{
    auto* layout = new QVBoxLayout(this);
    auto* barra = new QHBoxLayout;
    m_cerca = new QLineEdit(this);
    m_cerca->setPlaceholderText(QStringLiteral("Cerca per nome, codice o partita IVA"));
    m_cerca->setClearButtonEnabled(true);
    barra->addWidget(m_cerca, 1);
    auto* nuovo = new QPushButton(QStringLiteral("Nuovo fornitore"), this);
    auto* modifica = new QPushButton(QStringLiteral("Modifica"), this);
    auto* importa = new QPushButton(QStringLiteral("Importa da file…"), this);
    importa->setObjectName("importa_fornitori");
    barra->addWidget(nuovo);
    barra->addWidget(modifica);
    barra->addWidget(importa);
    barra->addWidget(pulsanteEsporta(this, QStringLiteral("fornitori"), [this] {
        return QList<FoglioXlsx>{m_tabella->foglio(QStringLiteral("Fornitori"))};
    }));
    layout->addLayout(barra);
    connect(importa, &QPushButton::clicked, this, [this] {
        importaDaFile(m_client, TipoImportazione::Fornitori, this, [this] { aggiorna(); });
    });
    m_tabella = new TabellaServer({
        {"codice", QStringLiteral("Codice")},
        {"ragione_sociale", QStringLiteral("Ragione sociale")},
        {"partita_iva", QStringLiteral("Partita IVA")},
        {"comune", QStringLiteral("Comune")},
        {"telefono", QStringLiteral("Telefono")},
        {"email", QStringLiteral("Email")},
        {"giorni_ordine", QStringLiteral("Ordina ogni"), ColonnaServer::Testo,
         [](const QJsonObject& f) {
             return f.value("giorni_ordine").isNull() ? QString() : QStringLiteral("%1 giorni").arg(f.value("giorni_ordine").toInt());
         }},
        {"attivo", QStringLiteral("Attivo"), ColonnaServer::SiNo},
    }, this);
    m_tabella->setObjectName("fornitori");
    m_tabella->testoVuoto = QStringLiteral("Nessun fornitore");
    layout->addWidget(m_tabella, 1);

    auto* ritardo = new QTimer(this);
    ritardo->setSingleShot(true);
    ritardo->setInterval(400);
    connect(m_cerca, &QLineEdit::textChanged, ritardo, qOverload<>(&QTimer::start));
    connect(ritardo, &QTimer::timeout, this, &VistaFornitori::aggiorna);
    connect(nuovo, &QPushButton::clicked, this, [this] { this->modifica({}); });
    connect(modifica, &QPushButton::clicked, this, [this] {
        if (const auto f = m_tabella->selezionata())
            this->modifica(*f);
    });
    connect(m_tabella, &TabellaServer::attivata, this, &VistaFornitori::modifica);
}

void VistaFornitori::aggiorna()
{
    QString risorsa = QStringLiteral("fornitori?select=*&order=ragione_sociale&limit=500");
    const QString t = m_cerca->text().trimmed();
    if (!t.isEmpty()) {
        QString v = QLatin1Char('*') + t + QLatin1Char('*');
        v.replace(QLatin1Char('"'), QStringLiteral("\\\""));
        v = QLatin1Char('"') + v + QLatin1Char('"');
        risorsa += QStringLiteral("&or=") + ClientServer::valoreFiltro(
            QStringLiteral("(ragione_sociale.ilike.%1,codice.ilike.%1,partita_iva.ilike.%1)").arg(v));
    }
    m_tabella->carica(m_client, risorsa);
}

void VistaFornitori::modifica(const QJsonObject& fornitore)
{
    DialogoCampi d(fornitore.isEmpty() ? QStringLiteral("Nuovo fornitore") : QStringLiteral("Modifica fornitore"),
                   campiFornitore, fornitore, this);
    if (d.exec() != QDialog::Accepted)
        return;
    QJsonObject v = d.valori();
    if (v.value("ragione_sociale").toString().isEmpty()) {
        QMessageBox::warning(this, QStringLiteral("Fornitori"), QStringLiteral("La ragione sociale è obbligatoria."));
        return;
    }
    // Partita IVA senza "IT" davanti: le fatture passive si abbinano su questa.
    v.insert("partita_iva", v.value("partita_iva").toString().toUpper().remove(QRegularExpression(QStringLiteral("^IT|\\s"))));
    if (v.value("partita_iva").toString().isEmpty())
        v.insert("partita_iva", QJsonValue());
    // Giorni da coprire nella proposta d'ordine: numero intero da 1 a 120, vuoto = 14
    if (const QString g = v.value("giorni_ordine").toString(); !g.isEmpty()) {
        bool ok = false;
        const int giorni = g.toInt(&ok);
        if (!ok || giorni < 1 || giorni > 120) {
            QMessageBox::warning(this, QStringLiteral("Fornitori"), QStringLiteral("«Si ordina ogni» va da 1 a 120 giorni (vuoto = 14)."));
            return;
        }
        v.insert("giorni_ordine", giorni);
    }
    QPointer<VistaFornitori> io(this);
    auto dopo = [io](const RispostaServer& r) {
        if (io && scritturaRiuscita(io, QStringLiteral("Fornitori"), r))
            io->aggiorna();
    };
    if (fornitore.isEmpty())
        m_client.inserisci(QStringLiteral("fornitori"), v, dopo);
    else
        m_client.modifica(QStringLiteral("fornitori"), QStringLiteral("id=eq.") + fornitore.value("id").toString(), v, dopo);
}

// ---------------------------------------------------------------- Operatori

VistaOperatori::VistaOperatori(ClientServer& client, QWidget* parent)
    : VistaServer(client, parent)
{
    auto* layout = new QVBoxLayout(this);
    auto* nota = new QLabel(QStringLiteral("Ogni persona entra con email e password (gestione, palmare) e si "
                                           "identifica alla cassa con codice e PIN. \"Nuovo utente\" crea tutto insieme."), this);
    nota->setWordWrap(true);
    layout->addWidget(nota);
    auto* barra = new QHBoxLayout;
    auto* nuovo = new QPushButton(style()->standardIcon(QStyle::SP_FileIcon), QStringLiteral("Nuovo utente…"), this);
    nuovo->setObjectName("nuovo_utente");
    auto* modifica = new QPushButton(QStringLiteral("Modifica"), this);
    auto* pin = new QPushButton(QStringLiteral("Imposta PIN…"), this);
    auto* password = new QPushButton(style()->standardIcon(QStyle::SP_DialogResetButton), QStringLiteral("Cambia password…"), this);
    auto* aggiorna = new QPushButton(style()->standardIcon(QStyle::SP_BrowserReload), QStringLiteral("Aggiorna"), this);
    barra->addWidget(nuovo);
    barra->addWidget(modifica);
    barra->addWidget(pin);
    barra->addWidget(password);
    barra->addStretch();
    barra->addWidget(aggiorna);
    layout->addLayout(barra);
    m_tabella = new TabellaServer({
        {"nome", QStringLiteral("Nome")},
        {"ruolo", QStringLiteral("Ruolo")},
        {"codice", QStringLiteral("Codice cassa")},
        {"pin", QStringLiteral("PIN"), ColonnaServer::Testo,
         [](const QJsonObject& o) {
             return valoreCampo(o, "operatori_pin.updated_at").isNull() ? QStringLiteral("non impostato") : QStringLiteral("impostato");
         }},
        {"attivo", QStringLiteral("Attivo"), ColonnaServer::SiNo},
    }, this);
    m_tabella->setObjectName("operatori");
    m_tabella->evidenzia = [](const QJsonObject& o) { return !o.value("attivo").toBool(); };
    layout->addWidget(m_tabella, 1);

    connect(modifica, &QPushButton::clicked, this, [this] {
        if (const auto o = m_tabella->selezionata())
            this->modifica(*o);
    });
    connect(m_tabella, &TabellaServer::attivata, this, &VistaOperatori::modifica);
    connect(pin, &QPushButton::clicked, this, &VistaOperatori::impostaPin);
    connect(nuovo, &QPushButton::clicked, this, &VistaOperatori::nuovoUtente);
    connect(password, &QPushButton::clicked, this, &VistaOperatori::cambiaPassword);
    connect(aggiorna, &QPushButton::clicked, this, &VistaOperatori::aggiorna);
}

void VistaOperatori::nuovoUtente()
{
    QDialog d(this);
    d.setWindowTitle(QStringLiteral("Nuovo utente"));
    auto* form = new QFormLayout(&d);
    auto* nome = new QLineEdit(&d);
    auto* email = new QLineEdit(&d);
    email->setPlaceholderText(QStringLiteral("es. mario@negozio.lan (non serve che esista davvero)"));
    auto* password = new QLineEdit(&d);
    password->setEchoMode(QLineEdit::Password);
    password->setPlaceholderText(QStringLiteral("almeno 6 caratteri"));
    auto* ruolo = new QComboBox(&d);
    for (const auto* r : {"amministratore", "responsabile", "magazziniere", "cassiere"})
        ruolo->addItem(QLatin1String(r), QLatin1String(r));
    ruolo->setToolTip(QStringLiteral("Amministratore: tutto (gestione, palmare, cassa, utenti)"));
    auto* codice = new QLineEdit(&d);
    codice->setPlaceholderText(QStringLiteral("facoltativo: 1-6 cifre, per la cassa"));
    auto* pin = new QLineEdit(&d);
    pin->setEchoMode(QLineEdit::Password);
    pin->setPlaceholderText(QStringLiteral("facoltativo: 4-8 cifre, per la cassa"));
    form->addRow(QStringLiteral("Nome"), nome);
    form->addRow(QStringLiteral("Email di accesso"), email);
    form->addRow(QStringLiteral("Password"), password);
    form->addRow(QStringLiteral("Ruolo"), ruolo);
    form->addRow(QStringLiteral("Codice cassa"), codice);
    form->addRow(QStringLiteral("PIN"), pin);
    auto* bottoni = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel, &d);
    form->addRow(bottoni);
    connect(bottoni, &QDialogButtonBox::accepted, &d, &QDialog::accept);
    connect(bottoni, &QDialogButtonBox::rejected, &d, &QDialog::reject);
    if (d.exec() != QDialog::Accepted)
        return;

    QString problema;
    if (nome->text().trimmed().isEmpty())
        problema = QStringLiteral("Manca il nome.");
    else if (!email->text().contains(QLatin1Char('@')))
        problema = QStringLiteral("Email non valida.");
    else if (password->text().size() < 6)
        problema = QStringLiteral("La password deve avere almeno 6 caratteri.");
    else if (!codice->text().trimmed().isEmpty()
             && !QRegularExpression(QStringLiteral("^[0-9]{1,6}$")).match(codice->text().trimmed()).hasMatch())
        problema = QStringLiteral("Il codice cassa è di 1-6 cifre.");
    else if (!pin->text().isEmpty() && !QRegularExpression(QStringLiteral("^[0-9]{4,8}$")).match(pin->text()).hasMatch())
        problema = QStringLiteral("Il PIN deve avere da 4 a 8 cifre.");
    if (!problema.isEmpty()) {
        QMessageBox::warning(this, QStringLiteral("Nuovo utente"), problema);
        return;
    }
    QJsonObject p{{"nome", nome->text().trimmed()}, {"email", email->text().trimmed()}, {"password", password->text()},
                  {"ruolo", ruolo->currentData().toString()}};
    if (!codice->text().trimmed().isEmpty())
        p.insert("codice", codice->text().trimmed());
    if (!pin->text().isEmpty())
        p.insert("pin", pin->text());
    QPointer<VistaOperatori> io(this);
    const QString chi = email->text().trimmed();
    m_client.rpc(QStringLiteral("crea_utente"), {{"p", p}}, [io, chi](const RispostaServer& r) {
        if (!io)
            return;
        if (!r.ok()) {
            mostraErroreServer(io, QStringLiteral("Nuovo utente"), r);
            return;
        }
        QMessageBox::information(io, QStringLiteral("Nuovo utente"),
                                 QStringLiteral("Creato: %1 può accedere subito alla gestione e al palmare; "
                                                "codice e PIN arrivano alle casse con la sincronizzazione.").arg(chi));
        io->aggiorna();
    });
}

void VistaOperatori::cambiaPassword()
{
    const auto o = m_tabella->selezionata();
    if (!o) {
        QMessageBox::information(this, QStringLiteral("Password"), QStringLiteral("Selezionare prima l'operatore."));
        return;
    }
    bool ok = false;
    const QString pw = QInputDialog::getText(this, QStringLiteral("Password"),
                                             QStringLiteral("Nuova password di accesso per %1 (almeno 6 caratteri):")
                                                 .arg(o->value("nome").toString()),
                                             QLineEdit::Password, {}, &ok);
    if (!ok)
        return;
    if (pw.size() < 6) {
        QMessageBox::warning(this, QStringLiteral("Password"), QStringLiteral("La password deve avere almeno 6 caratteri."));
        return;
    }
    QPointer<VistaOperatori> io(this);
    m_client.rpc(QStringLiteral("cambia_password"), {{"p_utente_id", o->value("id").toString()}, {"p_password", pw}},
                 [io](const RispostaServer& r) {
        if (!io)
            return;
        if (!r.ok())
            mostraErroreServer(io, QStringLiteral("Password"), r);
        else
            QMessageBox::information(io, QStringLiteral("Password"), QStringLiteral("Password cambiata."));
    });
}

void VistaOperatori::aggiorna()
{
    m_tabella->carica(m_client, QStringLiteral("operatori?select=id,nome,ruolo,codice,attivo,operatori_pin(updated_at)&order=nome"));
}

void VistaOperatori::modifica(const QJsonObject& operatore)
{
    QDialog d(this);
    d.setWindowTitle(QStringLiteral("Operatore"));
    auto* form = new QFormLayout(&d);
    auto* nome = new QLineEdit(operatore.value("nome").toString(), &d);
    auto* ruolo = new QComboBox(&d);
    for (const auto* r : {"cassiere", "magazziniere", "responsabile", "amministratore"})
        ruolo->addItem(QLatin1String(r), QLatin1String(r));
    ruolo->setCurrentIndex(std::max(0, ruolo->findData(operatore.value("ruolo").toString())));
    auto* codice = new QLineEdit(operatore.value("codice").toString(), &d);
    codice->setPlaceholderText(QStringLiteral("1-6 cifre, da digitare alla cassa"));
    auto* attivo = new QCheckBox(QStringLiteral("Attivo"), &d);
    attivo->setChecked(operatore.value("attivo").toBool());
    form->addRow(QStringLiteral("Nome"), nome);
    form->addRow(QStringLiteral("Ruolo"), ruolo);
    form->addRow(QStringLiteral("Codice cassa"), codice);
    form->addRow(QString(), attivo);
    auto* bottoni = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel, &d);
    form->addRow(bottoni);
    connect(bottoni, &QDialogButtonBox::accepted, &d, &QDialog::accept);
    connect(bottoni, &QDialogButtonBox::rejected, &d, &QDialog::reject);
    if (d.exec() != QDialog::Accepted)
        return;
    const QString c = codice->text().trimmed();
    if (!c.isEmpty() && !QRegularExpression(QStringLiteral("^[0-9]{1,6}$")).match(c).hasMatch()) {
        QMessageBox::warning(this, QStringLiteral("Operatore"), QStringLiteral("Il codice cassa è di 1-6 cifre."));
        return;
    }
    QPointer<VistaOperatori> io(this);
    m_client.modifica(QStringLiteral("operatori"), QStringLiteral("id=eq.") + operatore.value("id").toString(),
                      {{"nome", nome->text().trimmed()}, {"ruolo", ruolo->currentData().toString()},
                       {"codice", c.isEmpty() ? QJsonValue() : QJsonValue(c)}, {"attivo", attivo->isChecked()}},
                      [io](const RispostaServer& r) {
        if (io && scritturaRiuscita(io, QStringLiteral("Operatore"), r))
            io->aggiorna();
    });
}

void VistaOperatori::impostaPin()
{
    const auto o = m_tabella->selezionata();
    if (!o)
        return;
    bool ok = false;
    const QString pin = QInputDialog::getText(this, QStringLiteral("PIN"),
                                              QStringLiteral("Nuovo PIN per %1 (4-8 cifre):").arg(o->value("nome").toString()),
                                              QLineEdit::Password, {}, &ok);
    if (!ok)
        return;
    if (!QRegularExpression(QStringLiteral("^[0-9]{4,8}$")).match(pin).hasMatch()) {
        QMessageBox::warning(this, QStringLiteral("PIN"), QStringLiteral("Il PIN deve avere da 4 a 8 cifre."));
        return;
    }
    QPointer<VistaOperatori> io(this);
    m_client.rpc(QStringLiteral("imposta_pin"), {{"p_operatore_id", o->value("id").toString()}, {"p_pin", pin}},
                 [io](const RispostaServer& r) {
        if (!io)
            return;
        if (!r.ok())
            mostraErroreServer(io, QStringLiteral("PIN"), r);
        else
            QMessageBox::information(io, QStringLiteral("PIN"), QStringLiteral("PIN impostato: arriva alle casse con la sincronizzazione."));
        io->aggiorna();
    });
}

// ---------------------------------------------------------------- Negozio

VistaNegozio::VistaNegozio(ClientServer& client, QWidget* parent)
    : VistaServer(client, parent)
{
    auto* layout = new QVBoxLayout(this);
    auto* gruppo = new QGroupBox(QStringLiteral("Dati del negozio per le fatture elettroniche"), this);
    auto* form = new QFormLayout(gruppo);
    const QList<QPair<QString, QString>> campi = {
        {"ragione_sociale", "Ragione sociale"}, {"partita_iva", "Partita IVA"}, {"codice_fiscale", "Codice fiscale"},
        {"indirizzo", "Indirizzo"}, {"cap", "CAP"}, {"comune", "Comune"}, {"provincia", "Provincia"},
        {"nazione", "Nazione"}, {"regime_fiscale", "Regime fiscale"}, {"telefono", "Telefono"}, {"email", "Email"},
        {"pec", "PEC"}, {"iban", "IBAN"},
    };
    for (const auto& [campo, etichetta] : campi) {
        auto* e = new QLineEdit(gruppo);
        m_campi.insert(campo, e);
        form->addRow(etichetta, e);
    }
    m_campi["regime_fiscale"]->setToolTip(QStringLiteral("RF01 ordinario, RF19 forfettario…"));
    m_bollo = new QCheckBox(QStringLiteral("Addebitare al cliente il bollo da 2 € sulle fatture esenti oltre 77,47 €"), gruppo);
    form->addRow(QString(), m_bollo);
    layout->addWidget(gruppo);
    auto* barra = new QHBoxLayout;
    m_stato = new QLabel(this);
    barra->addWidget(m_stato, 1);
    m_salva = new QPushButton(QStringLiteral("Salva"), this);
    barra->addWidget(m_salva);
    layout->addLayout(barra);
    layout->addStretch();
    connect(m_salva, &QPushButton::clicked, this, &VistaNegozio::salva);
}

void VistaNegozio::aggiorna()
{
    m_stato->setText(QStringLiteral("Caricamento…"));
    QPointer<VistaNegozio> io(this);
    m_client.leggi(QStringLiteral("impostazioni?select=*"), [io](const RispostaServer& r) {
        if (!io)
            return;
        const auto righe = r.json().array();
        if (!r.ok() || righe.isEmpty()) {
            io->m_stato->setText(QStringLiteral("Errore: %1").arg(r.errore));
            return;
        }
        const auto o = righe.first().toObject();
        for (auto it = io->m_campi.begin(); it != io->m_campi.end(); ++it)
            it.value()->setText(o.value(it.key()).toString());
        io->m_bollo->setChecked(o.value("addebita_bollo").toBool());
        io->m_stato->setText(QString());
    });
}

void VistaNegozio::salva()
{
    QJsonObject v;
    for (auto it = m_campi.cbegin(); it != m_campi.cend(); ++it) {
        const auto t = it.value()->text().trimmed();
        v.insert(it.key(), t.isEmpty() ? QJsonValue() : QJsonValue(t));
    }
    // Colonne obbligatorie sul server.
    v.insert("ragione_sociale", m_campi["ragione_sociale"]->text().trimmed());
    if (v.value("nazione").isNull())
        v.insert("nazione", QStringLiteral("IT"));
    if (v.value("regime_fiscale").isNull())
        v.insert("regime_fiscale", QStringLiteral("RF01"));
    v.insert("addebita_bollo", m_bollo->isChecked());
    m_salva->setEnabled(false);
    QPointer<VistaNegozio> io(this);
    m_client.modifica(QStringLiteral("impostazioni"), QStringLiteral("id=eq.true"), v, [io](const RispostaServer& r) {
        if (!io)
            return;
        io->m_salva->setEnabled(true);
        if (scritturaRiuscita(io, QStringLiteral("Negozio"), r))
            io->m_stato->setText(QStringLiteral("Salvato."));
    });
}

} // namespace mdr
