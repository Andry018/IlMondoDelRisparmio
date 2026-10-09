#include "dialogooperatore.h"

#include "comune.h"
#include "locale/operazionicassa.h"

#include <QApplication>
#include <QGridLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QVBoxLayout>

namespace mdr {

DialogoOperatore::DialogoOperatore(ArchivioLocale& archivio, QWidget* parent)
    : QDialog(parent)
    , m_archivio(archivio)
{
    setWindowTitle(QStringLiteral("Operatore"));
    setMinimumSize(420, 520);
    auto* layout = new QVBoxLayout(this);
    auto* titolo = new QLabel(QStringLiteral("Chi è alla cassa?"), this);
    titolo->setFont(fontGrande(font(), 18, true));
    layout->addWidget(titolo);

    auto* campi = new QGridLayout;
    m_codice = new QLineEdit(this);
    m_codice->setObjectName("codice_operatore");
    m_codice->setPlaceholderText(QStringLiteral("Codice"));
    m_pin = new QLineEdit(this);
    m_pin->setObjectName("pin_operatore");
    m_pin->setPlaceholderText(QStringLiteral("PIN"));
    m_pin->setEchoMode(QLineEdit::Password);
    for (auto* e : {m_codice, m_pin}) {
        e->setFont(fontGrande(font(), 20));
        e->setMinimumHeight(52);
    }
    campi->addWidget(m_codice, 0, 0);
    campi->addWidget(m_pin, 0, 1);
    layout->addLayout(campi);

    m_errore = new QLabel(this);
    m_errore->setStyleSheet(QStringLiteral("color: #e53935;"));
    m_errore->setFont(fontGrande(font(), 12, true));
    layout->addWidget(m_errore);

    auto* tastierino = new QGridLayout;
    const QStringList tasti = {"7", "8", "9", "4", "5", "6", "1", "2", "3", "C", "0", QStringLiteral("←")};
    for (int i = 0; i < tasti.size(); ++i) {
        auto* b = new QPushButton(tasti[i], this);
        b->setFocusPolicy(Qt::NoFocus);
        b->setMinimumSize(80, 64);
        b->setFont(fontGrande(font(), 18, true));
        tastierino->addWidget(b, i / 3, i % 3);
        connect(b, &QPushButton::clicked, this, [this, t = tasti[i]] { tasto(t); });
    }
    layout->addLayout(tastierino, 1);

    auto* ok = new QPushButton(QStringLiteral("Entra"), this);
    ok->setObjectName("entra");
    ok->setMinimumHeight(56);
    ok->setStyleSheet("QPushButton { background: #2e7d32; color: white; font-weight: bold; }");
    ok->setDefault(true);
    layout->addWidget(ok);

    // Il tastierino scrive nel campo selezionato; Invio sul codice passa al PIN.
    m_attivo = m_codice;
    connect(m_codice, &QLineEdit::returnPressed, this, [this] { m_pin->setFocus(); m_attivo = m_pin; });
    connect(m_pin, &QLineEdit::returnPressed, this, &DialogoOperatore::conferma);
    connect(ok, &QPushButton::clicked, this, &DialogoOperatore::conferma);
    connect(qApp, &QApplication::focusChanged, this, [this](QWidget*, QWidget* ora) {
        if (ora == m_codice || ora == m_pin)
            m_attivo = static_cast<QLineEdit*>(ora);
    });
    m_codice->setFocus();
}

void DialogoOperatore::tasto(const QString& t)
{
    if (t == QLatin1String("C"))
        m_attivo->clear();
    else if (t == QStringLiteral("←"))
        m_attivo->backspace();
    else
        m_attivo->insert(t);
    m_errore->clear();
}

bool DialogoOperatore::prova(const QString& codice, const QString& pin)
{
    const auto o = m_archivio.operatore(codice);
    if (!o || !ArchivioLocale::verificaPin(*o, pin)) {
        m_errore->setText(QStringLiteral("Codice o PIN errato"));
        m_pin->clear();
        m_pin->setFocus();
        m_attivo = m_pin;
        return false;
    }
    m_operatore = o;
    return true;
}

void DialogoOperatore::conferma()
{
    if (prova(m_codice->text().trimmed(), m_pin->text()))
        accept();
}

bool assicuraOperatore(QWidget* parent, ArchivioLocale& archivio, OperazioniCassa& operazioni)
{
    if (!operazioni.operatore().isNull() || archivio.contaOperatori() == 0)
        return true;
    DialogoOperatore d(archivio, parent);
    if (d.exec() != QDialog::Accepted || !d.operatore())
        return false;
    operazioni.impostaOperatore(d.operatore()->id);
    return true;
}

} // namespace mdr
