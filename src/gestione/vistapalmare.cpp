#include "vistapalmare.h"

#include "comune.h"
#include "impostazioni.h"

#include <qrcodegen.hpp>

#include <QComboBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHostAddress>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QNetworkInterface>
#include <QPainter>
#include <QPushButton>
#include <QUrl>
#include <QVBoxLayout>

namespace mdr {

QString testoQrPalmare(const QString& server, const QString& apiKey, const QString& token)
{
    QJsonObject o{{"server", server}, {"apikey", apiKey}};
    if (!token.trimmed().isEmpty())
        o.insert("token", token.trimmed());
    const auto json = QJsonDocument(o).toJson(QJsonDocument::Compact);
    return QStringLiteral("mdr:") + QString::fromLatin1(json.toBase64(QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals));
}

bool serverLocale(const QString& server)
{
    const QString host = QUrl(server).host().toLower();
    return host == QLatin1String("localhost") || host.startsWith(QLatin1String("127.")) || host == QLatin1String("::1");
}

QString serverPerPalmare(const QString& server, const QString& indirizzoRete)
{
    if (!serverLocale(server) || indirizzoRete.isEmpty())
        return server;
    QUrl u(server);
    u.setHost(indirizzoRete);
    return u.toString(QUrl::StripTrailingSlash);
}

QStringList indirizziRete()
{
    static const QStringList virtuali = {"vethernet", "wsl", "hyper-v", "virtualbox", "vmware", "loopback"};
    QStringList primi, altri;
    for (const auto& i : QNetworkInterface::allInterfaces()) {
        const auto f = i.flags();
        if (!f.testFlag(QNetworkInterface::IsUp) || !f.testFlag(QNetworkInterface::IsRunning)
            || f.testFlag(QNetworkInterface::IsLoopBack))
            continue;
        const QString nome = (i.humanReadableName() + QLatin1Char(' ') + i.name()).toLower();
        if (std::any_of(virtuali.cbegin(), virtuali.cend(), [&](const QString& v) { return nome.contains(v); }))
            continue;
        const bool fisica = i.type() == QNetworkInterface::Wifi || i.type() == QNetworkInterface::Ethernet;
        for (const auto& e : i.addressEntries()) {
            const auto ip = e.ip();
            if (ip.protocol() != QAbstractSocket::IPv4Protocol || ip.isLoopback() || ip.isLinkLocal())
                continue;
            (fisica ? primi : altri) << ip.toString();
        }
    }
    return primi + altri;
}

QImage immagineQr(const QString& testo, int lato)
{
    const auto qr = qrcodegen::QrCode::encodeText(testo.toUtf8().constData(), qrcodegen::QrCode::Ecc::MEDIUM);
    const int margine = 4;   // "quiet zone" dello standard
    const int moduli = qr.getSize() + 2 * margine;
    const int passo = std::max(1, lato / moduli);
    QImage img(moduli * passo, moduli * passo, QImage::Format_RGB32);
    img.fill(Qt::white);
    QPainter p(&img);
    for (int y = 0; y < qr.getSize(); ++y)
        for (int x = 0; x < qr.getSize(); ++x)
            if (qr.getModule(x, y))
                p.fillRect((x + margine) * passo, (y + margine) * passo, passo, passo, Qt::black);
    return img;
}

VistaPalmare::VistaPalmare(QWidget* parent)
    : QWidget(parent)
{
    auto* principale = new QHBoxLayout(this);

    auto* sinistra = new QVBoxLayout;
    auto* titolo = new QLabel(QStringLiteral("<h2>Collega un palmare</h2>"
        "<p>Sul palmare, nella schermata <b>Server</b> (al primo avvio, o Profilo → Server), leggere questo "
        "codice con il lettore o con <i>Leggi il QR con la fotocamera</i>: indirizzo, chiave e token "
        "si compilano da soli.</p>"), this);
    titolo->setWordWrap(true);
    sinistra->addWidget(titolo);

    auto* modulo = new QFormLayout;
    m_indirizzi = new QComboBox(this);
    m_indirizzi->setObjectName("indirizzo_palmare");
    m_indirizzi->setToolTip(QStringLiteral("Indirizzo di questo PC sulla rete del negozio"));
    m_token = new QLineEdit(this);
    m_token->setObjectName("token_palmare");
    m_token->setEchoMode(QLineEdit::Password);
    m_token->setPlaceholderText(QStringLiteral("facoltativo: aggiornamenti automatici del palmare"));
    modulo->addRow(QStringLiteral("Indirizzo del PC:"), m_indirizzi);
    modulo->addRow(QStringLiteral("Token aggiornamenti:"), m_token);
    sinistra->addLayout(modulo);

    m_dettagli = new QLabel(this);
    m_dettagli->setObjectName("dettagli_palmare");
    m_dettagli->setWordWrap(true);
    m_dettagli->setTextInteractionFlags(Qt::TextSelectableByMouse);
    sinistra->addWidget(m_dettagli);

    auto* bAggiorna = new QPushButton(QStringLiteral("Aggiorna (rete cambiata)"), this);
    connect(bAggiorna, &QPushButton::clicked, this, &VistaPalmare::aggiorna);
    sinistra->addWidget(bAggiorna, 0, Qt::AlignLeft);
    sinistra->addStretch();
    principale->addLayout(sinistra, 1);

    m_qr = new QLabel(this);
    m_qr->setObjectName("qr_palmare");
    m_qr->setAlignment(Qt::AlignCenter);
    m_qr->setMinimumSize(360, 360);
    principale->addWidget(m_qr, 1);

    connect(m_indirizzi, &QComboBox::currentTextChanged, this, &VistaPalmare::disegna);
    connect(m_token, &QLineEdit::textChanged, this, &VistaPalmare::disegna);
}

void VistaPalmare::impostazioni(const Impostazioni& imp)
{
    m_server = imp.server.trimmed();
    m_apiKey = imp.apiKey.trimmed();
    if (m_token->text().isEmpty())
        m_token->setText(imp.tokenAggiornamenti);   // lo stesso token di sola lettura delle casse
    aggiorna();
}

void VistaPalmare::aggiorna()
{
    const QString prima = m_indirizzi->currentText();
    m_indirizzi->blockSignals(true);
    m_indirizzi->clear();
    if (serverLocale(m_server)) {
        m_indirizzi->addItems(indirizziRete());
        const int i = m_indirizzi->findText(prima);
        if (i >= 0)
            m_indirizzi->setCurrentIndex(i);
    }
    m_indirizzi->setEnabled(serverLocale(m_server) && m_indirizzi->count() > 1);
    m_indirizzi->blockSignals(false);
    disegna();
}

void VistaPalmare::disegna()
{
    if (m_server.isEmpty() || m_apiKey.isEmpty()) {
        m_testo.clear();
        m_qr->setPixmap({});
        m_qr->setText(QStringLiteral("Prima configurare il server nella scheda Impostazioni."));
        m_dettagli->clear();
        return;
    }
    if (serverLocale(m_server) && m_indirizzi->count() == 0) {
        m_testo.clear();
        m_qr->setPixmap({});
        m_qr->setText(QStringLiteral("Nessuna rete: collegare il PC al Wi-Fi o al cavo del negozio."));
        m_dettagli->clear();
        return;
    }
    const QString server = serverPerPalmare(m_server, m_indirizzi->currentText());
    m_testo = testoQrPalmare(server, m_apiKey, m_token->text());
    m_qr->setPixmap(QPixmap::fromImage(immagineQr(m_testo, 360)));
    QString dettagli = QStringLiteral("Il palmare userà: <b>%1</b>%2")
                           .arg(server.toHtmlEscaped(),
                                m_token->text().trimmed().isEmpty() ? QStringLiteral(" (senza aggiornamenti automatici)")
                                                                     : QStringLiteral(" · con token degli aggiornamenti"));
    if (serverLocale(m_server))
        dettagli += QStringLiteral("<br><br>Il server gira su questo PC: il palmare deve essere sulla stessa rete e la "
                                   "porta %1 deve essere raggiungibile dalla rete (firewall; con Supabase in WSL, "
                                   "inoltro della porta o rete \"mirrored\").")
                        .arg(QUrl(m_server).port(80));
    m_dettagli->setText(dettagli);
}

} // namespace mdr
