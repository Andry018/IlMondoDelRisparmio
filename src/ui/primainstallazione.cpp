#include "primainstallazione.h"

#include "comune.h"
#include "fiscal/customxml.h"
#include "locale/clientserver.h"
#include "locale/ricercaserver.h"

#include <QButtonGroup>
#include <QComboBox>
#include <QFile>
#include <QFileDialog>
#include <QFormLayout>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QHostAddress>
#include <QLabel>
#include <QLineEdit>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPointer>
#include <QPushButton>
#include <QRadioButton>
#include <QRegularExpression>
#include <QUrl>
#include <QVBoxLayout>

namespace mdr {

namespace {

// Pagina con la condizione per andare avanti scritta da fuori (la decide la procedura).
class PaginaGuidata : public QWizardPage {
public:
    PaginaGuidata(const QString& titolo, const QString& spiegazione, std::function<bool()> completa = {})
        : m_completa(std::move(completa))
    {
        setTitle(titolo);
        setSubTitle(spiegazione);
    }
    bool isComplete() const override { return !m_completa || m_completa(); }
    void cambiata() { emit completeChanged(); }

private:
    std::function<bool()> m_completa;
};

bool indirizzoRtValido(const QString& indirizzo)
{
    // "192.168.1.50" oppure "192.168.1.50:8080" (come nelle Impostazioni)
    // Quattro numeri: QHostAddress accetterebbe anche "192.168.1" (abbreviato, = 192.168.0.1)
    const auto parti = indirizzo.split(QLatin1Char(':'));
    if (parti.size() > 2 || parti[0].count(QLatin1Char('.')) != 3
        || QHostAddress(parti[0]).protocol() != QAbstractSocket::IPv4Protocol)
        return false;
    if (parti.size() == 2) {
        const int porta = parti[1].toInt();
        return porta > 0 && porta < 65536;
    }
    return true;
}

// "192.168.1.10" → "http://192.168.1.10:8000": la porta del server del negozio è 8000.
QString completaIndirizzo(QString testo)
{
    testo = testo.trimmed();
    while (testo.endsWith(QLatin1Char('/')))
        testo.chop(1);
    if (testo.isEmpty())
        return testo;
    if (!testo.contains(QStringLiteral("://")))
        testo.prepend(QStringLiteral("http://"));
    QUrl url(testo);
    if (url.isValid() && url.port() == -1 && url.path().isEmpty() && url.scheme() == QLatin1String("http"))
        url.setPort(8000);
    return url.toString();
}

EsitoProvaRt provaRtVero(const QString& indirizzo)
{
    StampanteCustomXml rt(std::make_unique<TrasportoHttpCustom>(indirizzo), ConfigurazioneCustom::predefinita());
    EsitoProvaRt esito;
    esito.ok = rt.leggiInformazioni(&esito.errore, 5000);
    esito.matricola = rt.matricola();
    esito.descrizione = rt.descrizioneInformazioni();
    esito.addestramento = rt.inAddestramento();
    return esito;
}

void imposta(QLabel* etichetta, const QString& testo, bool errore)
{
    etichetta->setText(testo);
    etichetta->setStyleSheet(errore ? QStringLiteral("color: #b91c1c;") : QString());
}

PaginaGuidata* pagina(QWizard* w, int id) { return static_cast<PaginaGuidata*>(w->page(id)); }

} // namespace

DatiChiavi leggiChiavi(const QString& testo)
{
    DatiChiavi d;
    QStringList server;
    static const QRegularExpression riga(QStringLiteral("^\\s*([a-z_]+)\\s*[=:]\\s*(\\S+)\\s*$"));
    for (const auto& r : testo.split(QLatin1Char('\n'))) {
        const auto m = riga.match(r.trimmed());
        if (!m.hasMatch())
            continue;
        if (m.captured(1) == QLatin1String("server"))
            server << m.captured(2);
        else if (m.captured(1) == QLatin1String("anon_key"))
            d.chiave = m.captured(2);
    }
    for (const auto& s : server)
        if (!s.contains(QStringLiteral("127.0.0.1")) && !s.contains(QStringLiteral("localhost")) && !s.contains(QLatin1Char('<'))) {
            d.server = s;
            break;
        }
    if (d.server.isEmpty() && !server.isEmpty() && !server.first().contains(QLatin1Char('<')))
        d.server = server.first();
    return d;
}

PrimaInstallazione::PrimaInstallazione(Postazione postazione, Impostazioni impostazioni, QWidget* parent)
    : QWizard(parent)
    , m_postazione(postazione)
    , m_imp(std::move(impostazioni))
    , m_provaRt(provaRtVero)
    , m_rete(new QNetworkAccessManager(this))
    , m_client(new ClientServer(this))
    , m_ricerca(new RicercaServer(this))
{
    const bool cassa = postazione == Postazione::Cassa;
    setObjectName("prima_installazione");
    setWindowTitle(cassa ? QStringLiteral("Prima installazione della cassa") : QStringLiteral("Prima installazione della gestione"));
    setWizardStyle(QWizard::ModernStyle);
    setOption(QWizard::NoBackButtonOnStartPage);
    setButtonText(QWizard::NextButton, QStringLiteral("Avanti"));
    setButtonText(QWizard::BackButton, QStringLiteral("Indietro"));
    setButtonText(QWizard::FinishButton, QStringLiteral("Inizia a lavorare"));
    setButtonText(QWizard::CancelButton, QStringLiteral("Più tardi"));
    resize(640, 520);   // sta anche nello schermo del POS (1024x768)

    // 1. Benvenuto
    auto* benvenuto = new PaginaGuidata(QStringLiteral("Benvenuto"),
                                        QStringLiteral("Pochi passi per preparare questo PC. Si può cambiare tutto anche "
                                                       "dopo, nella scheda Impostazioni."));
    auto* lb = new QVBoxLayout(benvenuto);
    m_modo = new QButtonGroup(this);
    auto* negozio = new QRadioButton(QStringLiteral("Collegare al server del negozio"), benvenuto);
    negozio->setObjectName("modo_negozio");
    auto* prova = new QRadioButton(QStringLiteral("Solo per provare, senza server (articoli di esempio)"), benvenuto);
    prova->setObjectName("modo_prova");
    m_modo->addButton(negozio, 0);
    m_modo->addButton(prova, 1);
    negozio->setChecked(true);
    lb->addWidget(negozio);
    lb->addWidget(nota(cassa ? QStringLiteral("Il server acceso nella rete del negozio si trova da solo. Servirà "
                                              "l'account della cassa (es. cassa1), creato dalla gestione in Operatori.")
                             : QStringLiteral("Il server acceso nella rete del negozio si trova da solo. Servirà il "
                                              "proprio account (es. mario) con la sua password."),
                       benvenuto));
    lb->addSpacing(12);
    lb->addWidget(prova);
    lb->addStretch();
    setPage(Benvenuto, benvenuto);

    // 2. Server e chiave
    auto* server = new PaginaGuidata(QStringLiteral("Server del negozio"),
                                     QStringLiteral("Il server si cerca da solo nella rete del negozio. Se non si trova, "
                                                    "scrivere il suo indirizzo: la chiave la dà il server."),
                                     [this] { return m_collegato; });
    auto* ls = new QVBoxLayout(server);
    auto* form = new QFormLayout;
    m_server = new QLineEdit(m_imp.server, server);
    m_server->setObjectName("guida_server");
    m_server->setPlaceholderText(QStringLiteral("es. 192.168.1.10 oppure http://192.168.1.10:8000"));
    m_chiave = new QLineEdit(m_imp.apiKey, server);
    m_chiave->setObjectName("guida_chiave");
    m_chiave->setPlaceholderText(QStringLiteral("si compila da sola (oppure anon_key di chiavi.txt)"));
    form->addRow(QStringLiteral("Indirizzo"), m_server);
    form->addRow(QStringLiteral("Chiave"), m_chiave);
    ls->addLayout(form);
    auto* tastiServer = new QHBoxLayout;
    auto* cerca = new QPushButton(QStringLiteral("Cerca il server"), server);
    cerca->setObjectName("guida_cerca_server");
    cerca->setDefault(true);
    auto* apri = new QPushButton(QStringLiteral("Apri chiavi.txt…"), server);
    apri->setObjectName("guida_apri_chiavi");
    apri->setToolTip(QStringLiteral("Se il server non si trova: indirizzo e chiave dal file config\\chiavi.txt del server"));
    auto* provaServer = new QPushButton(QStringLiteral("Prova collegamento"), server);
    provaServer->setObjectName("guida_prova_server");
    tastiServer->addWidget(cerca);
    tastiServer->addWidget(apri);
    tastiServer->addStretch();
    tastiServer->addWidget(provaServer);
    connect(cerca, &QPushButton::clicked, this, &PrimaInstallazione::cercaServer);
    connect(m_ricerca, &RicercaServer::finita, this, [this](const QList<ServerTrovato>& trovati) {
        if (trovati.isEmpty()) {
            imposta(m_statoServer, QStringLiteral("Nessun server trovato. È acceso? Questo PC è collegato alla rete del "
                                                  "negozio? Altrimenti scrivere l'indirizzo del server qui sopra."),
                    true);
            return;
        }
        if (m_server->text() != m_serverAllaRicerca)   // intanto è stato scritto un indirizzo: si rispetta
            return;
        QStringList altri;
        for (int i = 1; i < trovati.size(); ++i)
            altri << trovati[i].indirizzo;
        m_server->setText(trovati.first().indirizzo);
        m_chiave->setText(trovati.first().chiave);
        this->provaServer();
        if (!altri.isEmpty())
            imposta(m_statoServer, QStringLiteral("Trovato %1 (ci sono anche %2). Prova in corso…")
                                       .arg(trovati.first().indirizzo, altri.join(QStringLiteral(", "))), false);
    });
    connect(m_ricerca, &RicercaServer::presentazione, this, [this](const QUrl& indirizzo, const QString& chiave, const QString& errore) {
        if (completaIndirizzo(m_server->text()) != indirizzo.toString(QUrl::StripTrailingSlash))
            return;   // l'indirizzo è cambiato intanto
        if (chiave.isEmpty()) {
            imposta(m_statoServer, QStringLiteral("Il server non ha dato la chiave (%1): copiarla da chiavi.txt sul PC "
                                                  "del server, oppure «Apri chiavi.txt…».").arg(errore),
                    true);
            return;
        }
        m_chiave->setText(chiave);
        this->provaServer();
    });
    // Prima volta sulla pagina del server, indirizzo vuoto: si cerca subito
    connect(this, &QWizard::currentIdChanged, this, [this](int id) {
        if (id == Server && !m_cercato && m_server->text().trimmed().isEmpty()) {
            m_cercato = true;
            cercaServer();
        }
    });
    ls->addLayout(tastiServer);
    m_statoServer = new QLabel(server);
    m_statoServer->setObjectName("guida_stato_server");
    m_statoServer->setWordWrap(true);
    ls->addWidget(m_statoServer);
    ls->addStretch();
    setPage(Server, server);
    connect(apri, &QPushButton::clicked, this, [this] {
        const QString file = QFileDialog::getOpenFileName(this, QStringLiteral("File delle chiavi del server"), {},
                                                          QStringLiteral("chiavi.txt (*.txt);;Tutti i file (*)"));
        if (!file.isEmpty())
            caricaChiavi(file);
    });
    connect(provaServer, &QPushButton::clicked, this, &PrimaInstallazione::provaServer);
    for (auto* campo : {m_server, m_chiave})
        connect(campo, &QLineEdit::textEdited, this, [this] {
            // Cambiato dopo la prova: va riprovato
            m_collegato = completaIndirizzo(m_server->text()) == m_serverProvato && m_chiave->text().trimmed() == m_chiaveProvata
                          && !m_serverProvato.isEmpty();
            pagina(this, Server)->cambiata();
        });
    connect(m_server, &QLineEdit::returnPressed, this, &PrimaInstallazione::provaServer);
    connect(m_chiave, &QLineEdit::returnPressed, this, &PrimaInstallazione::provaServer);

    // 3. Accesso
    auto* accesso = new PaginaGuidata(QStringLiteral("Accesso"),
                                      cassa ? QStringLiteral("L'account della cassa (es. cassa1) e la sua password. Le "
                                                             "cassiere poi entrano col loro PIN: questo serve solo al PC.")
                                            : QStringLiteral("Il proprio account (es. mario) e la password. Gli account si "
                                                             "creano dalla gestione, nella scheda Operatori."),
                                      [this] { return autenticato(); });
    auto* la = new QVBoxLayout(accesso);
    auto* formAccesso = new QFormLayout;
    m_email = new QLineEdit(m_imp.email, accesso);
    m_email->setObjectName("guida_email");
    m_email->setPlaceholderText(cassa ? QStringLiteral("es. cassa1") : QStringLiteral("es. mario"));
    m_email->setToolTip(QStringLiteral("Nome breve (diventa nome@negozio.lan) oppure l'email completa"));
    m_password = new QLineEdit(accesso);
    m_password->setObjectName("guida_password");
    m_password->setEchoMode(QLineEdit::Password);
    formAccesso->addRow(QStringLiteral("Account"), m_email);
    formAccesso->addRow(QStringLiteral("Password"), m_password);
    la->addLayout(formAccesso);
    auto* tastiAccesso = new QHBoxLayout;
    auto* entra = new QPushButton(QStringLiteral("Accedi"), accesso);
    entra->setObjectName("guida_accedi");
    tastiAccesso->addStretch();
    tastiAccesso->addWidget(entra);
    la->addLayout(tastiAccesso);
    m_statoAccesso = new QLabel(accesso);
    m_statoAccesso->setObjectName("guida_stato_accesso");
    m_statoAccesso->setWordWrap(true);
    la->addWidget(m_statoAccesso);
    la->addStretch();
    setPage(Accesso, accesso);
    connect(entra, &QPushButton::clicked, this, &PrimaInstallazione::accedi);
    connect(m_password, &QLineEdit::returnPressed, this, &PrimaInstallazione::accedi);
    connect(m_client, &ClientServer::accessoRiuscito, this, [this](const QString& email) {
        m_imp.email = email;
        imposta(m_statoAccesso, QStringLiteral("Accesso riuscito come %1.").arg(email), false);
        pagina(this, Accesso)->cambiata();
    });
    connect(m_client, &ClientServer::accessoFallito, this, [this](const QString& errore) {
        m_imp.refreshToken.clear();
        imposta(m_statoAccesso, QStringLiteral("Accesso non riuscito: %1").arg(errore), true);
        pagina(this, Accesso)->cambiata();
    });
    connect(m_client, &ClientServer::refreshTokenCambiato, this, [this](const QString& token) {
        m_imp.refreshToken = token;
        pagina(this, Accesso)->cambiata();
    });

    // 4. Cassa: codice e registratore
    if (cassa) {
        auto* p = new PaginaGuidata(QStringLiteral("Cassa e registratore"),
                                    QStringLiteral("Ogni cassa ha il suo codice (CASSA1, CASSA2…): compare nei report e "
                                                   "nelle chiusure. Il registratore Custom si collega in rete."));
        auto* lc = new QVBoxLayout(p);
        auto* f = new QFormLayout;
        m_terminale = new QLineEdit(m_imp.terminale, p);
        m_terminale->setObjectName("guida_terminale");
        m_terminale->setMaxLength(20);
        m_rtConnessione = new QComboBox(p);
        m_rtConnessione->setObjectName("guida_rt_connessione");
        m_rtConnessione->addItem(QStringLiteral("Registratore in rete (Custom K3-F RT)"), int(ConnessioneRt::Rete));
        m_rtConnessione->addItem(QStringLiteral("Simulatore (prove, nessuno scontrino vero)"), int(ConnessioneRt::Simulatore));
        m_rtConnessione->setCurrentIndex(m_imp.rtConnessione == ConnessioneRt::Rete ? 0 : 1);
        m_rtIndirizzo = new QLineEdit(m_imp.rtIndirizzo, p);
        m_rtIndirizzo->setObjectName("guida_rt_indirizzo");
        m_rtIndirizzo->setPlaceholderText(QStringLiteral("es. 192.168.1.50 (si legge dal registratore: menu Rete)"));
        f->addRow(QStringLiteral("Codice della cassa"), m_terminale);
        f->addRow(QStringLiteral("Registratore"), m_rtConnessione);
        f->addRow(QStringLiteral("Indirizzo IP"), m_rtIndirizzo);
        lc->addLayout(f);
        auto* tasti = new QHBoxLayout;
        auto* provaRt = new QPushButton(QStringLiteral("Prova registratore"), p);
        provaRt->setObjectName("guida_prova_rt");
        tasti->addStretch();
        tasti->addWidget(provaRt);
        lc->addLayout(tasti);
        m_statoRt = new QLabel(p);
        m_statoRt->setObjectName("guida_stato_rt");
        m_statoRt->setWordWrap(true);
        m_statoRt->setTextFormat(Qt::PlainText);
        lc->addWidget(m_statoRt);
        lc->addWidget(nota(QStringLiteral("Il collegamento USB non è supportato: si usa il cavo di rete. Se il registratore "
                                          "adesso è spento si può andare avanti e provarlo poi dalle Impostazioni."),
                           p));
        lc->addStretch();
        setPage(Cassa, p);
        auto campiRt = [this, provaRt] {
            const bool rete = m_rtConnessione->currentData().toInt() == int(ConnessioneRt::Rete);
            m_rtIndirizzo->setEnabled(rete);
            provaRt->setEnabled(rete);
        };
        connect(m_rtConnessione, &QComboBox::currentIndexChanged, this, campiRt);
        campiRt();
        connect(provaRt, &QPushButton::clicked, this, &PrimaInstallazione::provaRegistratore);
    }

    // 5. Riepilogo
    auto* fine = new PaginaGuidata(QStringLiteral("Tutto pronto"), QStringLiteral("Ecco come è stato preparato questo PC."));
    auto* lf = new QVBoxLayout(fine);
    m_riepilogo = new QLabel(fine);
    m_riepilogo->setObjectName("guida_riepilogo");
    m_riepilogo->setWordWrap(true);
    m_riepilogo->setTextFormat(Qt::RichText);
    lf->addWidget(m_riepilogo);
    lf->addStretch();
    setPage(Fine, fine);

    connect(this, &QWizard::currentIdChanged, this, [this](int id) {
        if (id == Accesso)
            m_client->configura(QUrl(m_serverProvato), m_chiaveProvata);
        if (id == Fine)
            preparaFine();
    });
    setStartId(Benvenuto);
}

PrimaInstallazione::~PrimaInstallazione() = default;

bool PrimaInstallazione::soloProva() const { return m_modo->checkedId() == 1; }

int PrimaInstallazione::nextId() const
{
    const bool cassa = m_postazione == Postazione::Cassa;
    switch (currentId()) {
    case Benvenuto: return soloProva() ? (cassa ? Cassa : Fine) : Server;
    case Server: return Accesso;
    case Accesso: return cassa ? Cassa : Fine;
    case Cassa: return Fine;
    default: return -1;
    }
}

QString PrimaInstallazione::controllaCassa() const
{
    static const QRegularExpression codice(QStringLiteral("^[A-Z0-9_-]{1,20}$"));
    const QString t = m_terminale->text().trimmed().toUpper();
    if (!codice.match(t).hasMatch())
        return QStringLiteral("Codice della cassa: lettere, numeri, - e _ (es. CASSA1).");
    if (m_rtConnessione->currentData().toInt() == int(ConnessioneRt::Rete) && !indirizzoRtValido(m_rtIndirizzo->text().trimmed()))
        return QStringLiteral("Indirizzo IP del registratore non valido, es. 192.168.1.50.");
    return {};
}

bool PrimaInstallazione::validateCurrentPage()
{
    if (currentId() == Cassa) {
        if (const QString errore = controllaCassa(); !errore.isEmpty()) {
            imposta(m_statoRt, errore, true);
            return false;
        }
    }
    return QWizard::validateCurrentPage();
}

void PrimaInstallazione::caricaChiavi(const QString& file)
{
    QFile f(file);
    if (!f.open(QIODevice::ReadOnly)) {
        imposta(m_statoServer, QStringLiteral("File non letto: %1").arg(f.errorString()), true);
        return;
    }
    const auto d = leggiChiavi(QString::fromUtf8(f.readAll()));
    if (d.chiave.isEmpty()) {
        imposta(m_statoServer, QStringLiteral("Nel file non c'è la riga anon_key: è il chiavi.txt del server?"), true);
        return;
    }
    if (!d.server.isEmpty())
        m_server->setText(d.server);
    m_chiave->setText(d.chiave);
    provaServer();
}

void PrimaInstallazione::provaServer()
{
    const QString indirizzo = completaIndirizzo(m_server->text());
    const QString chiave = m_chiave->text().trimmed();
    const QUrl url(indirizzo, QUrl::StrictMode);
    m_collegato = false;
    pagina(this, Server)->cambiata();
    if (indirizzo.isEmpty() || !url.isValid() || (url.scheme() != QLatin1String("http") && url.scheme() != QLatin1String("https"))) {
        imposta(m_statoServer, QStringLiteral("Indirizzo non valido: es. 192.168.1.10 oppure http://192.168.1.10:8000."), true);
        return;
    }
    if (chiave.isEmpty()) {
        // La chiave la dà il server stesso (/mdr/presentazione): chi installa scrive solo l'indirizzo
        m_server->setText(indirizzo);
        imposta(m_statoServer, QStringLiteral("Chiedo la chiave al server…"), false);
        m_ricerca->chiedi(QUrl(indirizzo));
        return;
    }
    m_server->setText(indirizzo);
    imposta(m_statoServer, QStringLiteral("Prova in corso…"), false);
    // La chiave si controlla all'ingresso del server (Caddy, o Kong su Supabase): 401 = sbagliata
    QNetworkRequest r(QUrl(indirizzo + QStringLiteral("/rest/v1/")));
    r.setRawHeader("apikey", chiave.toUtf8());
    r.setTransferTimeout(5000);
    auto* risposta = m_rete->get(r);
    QPointer<PrimaInstallazione> io(this);
    connect(risposta, &QNetworkReply::finished, this, [io, risposta, indirizzo, chiave] {
        risposta->deleteLater();
        if (!io)
            return;
        const int stato = risposta->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (stato == 0) {
            imposta(io->m_statoServer,
                    QStringLiteral("Server non raggiungibile (%1). È acceso? L'indirizzo è giusto? Questo PC è nella rete "
                                   "del negozio?").arg(risposta->errorString()),
                    true);
        } else if (stato == 401 || stato == 403) {
            imposta(io->m_statoServer, QStringLiteral("Il server risponde ma la chiave è sbagliata: ricopiarla da chiavi.txt."), true);
        } else if (stato >= 500) {
            imposta(io->m_statoServer, QStringLiteral("Il server risponde con un errore (%1): riprovare tra poco.").arg(stato), true);
        } else {
            io->m_collegato = true;
            io->m_serverProvato = indirizzo;
            io->m_chiaveProvata = chiave;
            imposta(io->m_statoServer, QStringLiteral("Collegamento riuscito."), false);
        }
        pagina(io, Server)->cambiata();
    });
}

void PrimaInstallazione::cercaServer()
{
    if (m_ricerca->inCorso())
        return;
    m_serverAllaRicerca = m_server->text();
    m_collegato = false;
    pagina(this, Server)->cambiata();
    imposta(m_statoServer, QStringLiteral("Cerco il server nella rete del negozio…"), false);
    // Porta di un indirizzo già scritto (es. :8100), altrimenti quella solita
    const QUrl scritto(completaIndirizzo(m_server->text()));
    m_ricerca->cerca(quint16(scritto.port(RicercaServer::portaPredefinita)));
}

void PrimaInstallazione::accedi()
{
    if (m_email->text().trimmed().isEmpty() || m_password->text().isEmpty()) {
        imposta(m_statoAccesso, QStringLiteral("Scrivere account e password."), true);
        return;
    }
    m_imp.refreshToken.clear();
    pagina(this, Accesso)->cambiata();
    imposta(m_statoAccesso, QStringLiteral("Accesso in corso…"), false);
    m_client->accedi(m_email->text().trimmed(), m_password->text());
}

void PrimaInstallazione::provaRegistratore()
{
    if (const QString errore = controllaCassa(); !errore.isEmpty()) {
        imposta(m_statoRt, errore, true);
        return;
    }
    const QString indirizzo = m_rtIndirizzo->text().trimmed();
    imposta(m_statoRt, QStringLiteral("Prova in corso…"), false);
    QGuiApplication::setOverrideCursor(Qt::WaitCursor);
    const auto esito = m_provaRt(indirizzo);
    QGuiApplication::restoreOverrideCursor();
    if (!esito.ok) {
        m_rtProvato.clear();
        imposta(m_statoRt, QStringLiteral("Il registratore non risponde: %1").arg(esito.errore), true);
        return;
    }
    m_rtProvato = indirizzo;
    m_imp.rtMatricola = esito.matricola;
    QString testo = QStringLiteral("Registratore %1 raggiungibile.").arg(esito.matricola);
    if (!esito.descrizione.isEmpty())
        testo += QLatin1Char('\n') + esito.descrizione;
    if (esito.addestramento)
        testo += QStringLiteral("\nÈ in modalità addestramento: gli scontrini non hanno valore fiscale.");
    imposta(m_statoRt, testo, esito.addestramento);
}

Impostazioni PrimaInstallazione::impostazioni() const
{
    Impostazioni imp = m_imp;
    if (soloProva()) {
        imp.server.clear();
        imp.apiKey.clear();
        imp.email.clear();
        imp.refreshToken.clear();
    } else {
        imp.server = m_serverProvato;
        imp.apiKey = m_chiaveProvata;
    }
    if (m_terminale) {
        imp.terminale = m_terminale->text().trimmed().toUpper();
        imp.rtConnessione = static_cast<ConnessioneRt>(m_rtConnessione->currentData().toInt());
        imp.rtIndirizzo = imp.rtConnessione == ConnessioneRt::Rete ? m_rtIndirizzo->text().trimmed() : QString();
    }
    return imp;
}

void PrimaInstallazione::preparaFine()
{
    const auto imp = impostazioni();
    QStringList righe;
    if (soloProva()) {
        righe << QStringLiteral("<b>Postazione di prova</b>: niente server, si parte dagli articoli di esempio. Le vendite "
                                "restano su questo PC.");
    } else {
        righe << QStringLiteral("<b>Server</b>: %1").arg(imp.server.toHtmlEscaped());
        righe << QStringLiteral("<b>Account</b>: %1").arg(imp.email.toHtmlEscaped());
    }
    if (m_terminale) {
        righe << QStringLiteral("<b>Cassa</b>: %1").arg(imp.terminale.toHtmlEscaped());
        if (imp.rtConnessione == ConnessioneRt::Rete) {
            const bool provato = m_rtProvato == imp.rtIndirizzo;
            righe << QStringLiteral("<b>Registratore</b>: in rete, %1 — %2")
                         .arg(imp.rtIndirizzo.toHtmlEscaped(),
                              provato ? QStringLiteral("provato, matricola %1").arg(imp.rtMatricola.toHtmlEscaped())
                                      : QStringLiteral("<span style=\"color:#b91c1c\">non provato</span>: provarlo dalle "
                                                       "Impostazioni prima del primo scontrino"));
        } else {
            righe << QStringLiteral("<b>Registratore</b>: simulatore (nessuno scontrino vero)");
        }
    }
    QString dopo = m_terminale ? QStringLiteral("Dopo: in Impostazioni si controllano i codici dei pagamenti e delle aliquote "
                                                "programmati sul registratore.")
                               : QStringLiteral("Dopo: gli account delle casse e delle persone si creano nella scheda "
                                                "Operatori; per i dati di SystemApp c'è la lista del passaggio "
                                                "(docs/PASSAGGIO.md).");
    m_riepilogo->setText(righe.join(QStringLiteral("<br><br>")) + QStringLiteral("<br><br><span style=\"color:#57606a\">%1</span>").arg(dopo));
}

} // namespace mdr
