#include "riepilogocommercialista.h"

#include "comune.h"
#include "core/money.h"
#include "locale/clientserver.h"

#include <QDateEdit>
#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QJsonObject>
#include <QLabel>
#include <QLocale>
#include <QMap>
#include <QMessageBox>
#include <QPointer>
#include <QPushButton>
#include <QStandardPaths>
#include <QTableWidget>
#include <QVBoxLayout>

#include <memory>

namespace mdr {

namespace {

QString e(qint64 centesimi)
{
    return Money::fromCents(centesimi).toString();
}

QString data(const QJsonValue& v)
{
    const QString t = v.toString();
    const QDate d = QDate::fromString(t.left(10), Qt::ISODate);
    return d.isValid() ? d.toString(QStringLiteral("dd/MM/yyyy")) : t;
}

QString nomeAliquota(int bp, const QString& natura)
{
    if (bp == 0)
        return natura.isEmpty() ? QStringLiteral("Esente") : QStringLiteral("Esente %1").arg(natura);
    return QStringLiteral("IVA %1%").arg(QString::number(bp / 100.0, 'f', bp % 100 ? 2 : 0).replace(QLatin1Char('.'), QLatin1Char(',')));
}

QString nomePagamento(const QString& codice)
{
    static const QMap<QString, QString> nomi = {
        {"contanti", "Contanti"}, {"carta", "Carta"}, {"bancomat", "Bancomat"}, {"satispay", "Satispay"},
        {"buono_pasto", "Buoni pasto"}, {"non_riscosso", "Non riscosso"}};
    return nomi.value(codice, codice);
}

// Le note di credito tolgono (fatture emesse: tipo nota_credito; ricevute: TD04)
int segno(const QJsonObject& r, bool ricevuta)
{
    return ricevuta ? (r.value("tipo_documento").toString() == QLatin1String("TD04") ? -1 : 1)
                    : (r.value("tipo").toString() == QLatin1String("nota_credito") ? -1 : 1);
}

} // namespace

QList<FoglioXlsx> fogliCommercialista(const DatiMese& d)
{
    const QLocale it(QLocale::Italian);

    // Corrispettivi per aliquota (ordinati: prima le aliquote più alte, poi le esenti)
    struct Somma {
        qint64 lordo = 0, imponibile = 0, imposta = 0;
    };
    QMap<QPair<int, QString>, Somma> perAliquota;
    QMap<QDate, QMap<QPair<int, QString>, qint64>> perGiorno;
    for (const auto& v : d.venduto) {
        const auto r = v.toObject();
        const QPair<int, QString> k{-r.value("aliquota_bp").toInt(), r.value("natura").toString()};
        auto& s = perAliquota[k];
        s.lordo += r.value("totale").toInteger();
        s.imponibile += r.value("imponibile").toInteger();
        s.imposta += r.value("imposta").toInteger();
        perGiorno[QDate::fromString(r.value("giorno").toString(), Qt::ISODate)][k] += r.value("totale").toInteger();
    }
    Somma totale;
    FoglioXlsx aliquote{QStringLiteral("Corrispettivi per aliquota"),
                        {QStringLiteral("Aliquota"), QStringLiteral("Lordo €"), QStringLiteral("Imponibile €"), QStringLiteral("Imposta €")},
                        {}};
    for (auto i = perAliquota.cbegin(); i != perAliquota.cend(); ++i) {
        aliquote.righe << QStringList{nomeAliquota(-i.key().first, i.key().second), e(i->lordo), e(i->imponibile), e(i->imposta)};
        totale.lordo += i->lordo;
        totale.imponibile += i->imponibile;
        totale.imposta += i->imposta;
    }
    aliquote.righe << QStringList{QStringLiteral("Totale"), e(totale.lordo), e(totale.imponibile), e(totale.imposta)};

    FoglioXlsx giorni{QStringLiteral("Corrispettivi per giorno"), {QStringLiteral("Giorno"), QStringLiteral("Totale €")}, {}};
    for (auto i = perAliquota.cbegin(); i != perAliquota.cend(); ++i)
        giorni.intestazioni << nomeAliquota(-i.key().first, i.key().second) + QStringLiteral(" €");
    for (auto g = perGiorno.cbegin(); g != perGiorno.cend(); ++g) {
        qint64 somma = 0;
        QStringList riga{g.key().toString(QStringLiteral("dd/MM/yyyy")), QString()};
        for (auto i = perAliquota.cbegin(); i != perAliquota.cend(); ++i) {
            const qint64 v = g->value(i.key());
            somma += v;
            riga << e(v);
        }
        riga[1] = e(somma);
        giorni.righe << riga;
    }

    // Incassi per giorno, cassa e pagamento; totali per pagamento nel riepilogo
    FoglioXlsx incassi{QStringLiteral("Incassi"),
                       {QStringLiteral("Giorno"), QStringLiteral("Cassa"), QStringLiteral("Pagamento"), QStringLiteral("Importo €")}, {}};
    QMap<QString, qint64> perPagamento;
    for (const auto& v : d.incassi) {
        const auto r = v.toObject();
        const QString tipo = r.value("tipo_pagamento").toString();
        incassi.righe << QStringList{data(r.value("giorno")), r.value("terminale_id").toString(), nomePagamento(tipo),
                                     e(r.value("importo").toInteger())};
        perPagamento[nomePagamento(tipo)] += r.value("importo").toInteger();
    }

    FoglioXlsx chiusure{QStringLiteral("Chiusure Z"),
                        {QStringLiteral("Data"), QStringLiteral("Cassa"), QStringLiteral("Matricola RT"), QStringLiteral("Azzeramento"),
                         QStringLiteral("Totale registratore €"), QStringLiteral("Totale server €"), QStringLiteral("Differenza €"),
                         QStringLiteral("Quadra")},
                        {}};
    int nonQuadrano = 0;
    for (const auto& v : d.chiusure) {
        const auto r = v.toObject();
        const bool quadra = r.value("quadra").toBool();
        nonQuadrano += !quadra;
        chiusure.righe << QStringList{data(r.value("data_ora")), r.value("terminale_id").toString(), r.value("rt_matricola").toString(),
                                      QString::number(r.value("rt_azzeramento").toInt()), e(r.value("totale_rt").toInteger()),
                                      e(r.value("totale_server").toInteger()), e(r.value("differenza").toInteger()),
                                      quadra ? QStringLiteral("sì") : QStringLiteral("no")};
    }

    FoglioXlsx emesse{QStringLiteral("Fatture emesse"),
                      {QStringLiteral("Numero"), QStringLiteral("Data"), QStringLiteral("Tipo"), QStringLiteral("Cliente"),
                       QStringLiteral("Imponibile €"), QStringLiteral("IVA €"), QStringLiteral("Totale €"), QStringLiteral("Stato SDI")},
                      {}};
    Somma totEmesse;
    for (const auto& v : d.fatture) {
        const auto r = v.toObject();
        const int s = segno(r, false);
        emesse.righe << QStringList{r.value("numero_documento").toString(), data(r.value("data")),
                                    s < 0 ? QStringLiteral("Nota di credito") : QStringLiteral("Fattura"), r.value("cliente").toString(),
                                    e(r.value("imponibile").toInteger()), e(r.value("imposta").toInteger()),
                                    e(r.value("totale").toInteger()), r.value("stato").toString()};
        totEmesse.imponibile += s * r.value("imponibile").toInteger();
        totEmesse.imposta += s * r.value("imposta").toInteger();
        totEmesse.lordo += s * r.value("totale").toInteger();
    }

    FoglioXlsx ricevute{QStringLiteral("Fatture ricevute"),
                        {QStringLiteral("Numero"), QStringLiteral("Data"), QStringLiteral("Tipo"), QStringLiteral("Fornitore"),
                         QStringLiteral("Partita IVA"), QStringLiteral("Imponibile €"), QStringLiteral("IVA €"), QStringLiteral("Totale €")},
                        {}};
    Somma totRicevute;
    for (const auto& v : d.passive) {
        const auto r = v.toObject();
        const int s = segno(r, true);
        ricevute.righe << QStringList{r.value("numero").toString(), data(r.value("data")), r.value("tipo_documento").toString(),
                                      r.value("fornitore_nome").toString(), r.value("fornitore_partita_iva").toString(),
                                      e(r.value("imponibile").toInteger()), e(r.value("imposta").toInteger()),
                                      e(r.value("totale").toInteger())};
        totRicevute.imponibile += s * r.value("imponibile").toInteger();
        totRicevute.imposta += s * r.value("imposta").toInteger();
        totRicevute.lordo += s * r.value("totale").toInteger();
    }

    // Riepilogo: le cifre che il commercialista cerca per prime
    FoglioXlsx riepilogo{QStringLiteral("Riepilogo"), {QStringLiteral("Voce"), QStringLiteral("Valore")}, {}};
    auto voce = [&](const QString& v, const QString& valore) { riepilogo.righe << QStringList{v, valore}; };
    voce(QStringLiteral("Mese"), it.toString(d.mese, QStringLiteral("MMMM yyyy")));
    voce(QStringLiteral("Corrispettivi (vendite meno resi) €"), e(totale.lordo));
    voce(QStringLiteral("Corrispettivi: imponibile €"), e(totale.imponibile));
    voce(QStringLiteral("Corrispettivi: IVA €"), e(totale.imposta));
    for (auto i = perAliquota.cbegin(); i != perAliquota.cend(); ++i)
        voce(QStringLiteral("  %1 (lordo) €").arg(nomeAliquota(-i.key().first, i.key().second)), e(i->lordo));
    for (auto p = perPagamento.cbegin(); p != perPagamento.cend(); ++p)
        voce(QStringLiteral("Incassato in %1 €").arg(p.key().toLower()), e(p.value()));
    voce(QStringLiteral("Chiusure Z"), QString::number(d.chiusure.size()));
    voce(QStringLiteral("Chiusure Z che non quadrano"), QString::number(nonQuadrano));
    voce(QStringLiteral("Fatture emesse (note di credito comprese)"), QString::number(d.fatture.size()));
    voce(QStringLiteral("Fatture emesse: imponibile €"), e(totEmesse.imponibile));
    voce(QStringLiteral("Fatture emesse: IVA €"), e(totEmesse.imposta));
    voce(QStringLiteral("Fatture emesse: totale €"), e(totEmesse.lordo));
    voce(QStringLiteral("Fatture ricevute (note di credito comprese)"), QString::number(d.passive.size()));
    voce(QStringLiteral("Fatture ricevute: imponibile €"), e(totRicevute.imponibile));
    voce(QStringLiteral("Fatture ricevute: IVA €"), e(totRicevute.imposta));
    voce(QStringLiteral("Fatture ricevute: totale €"), e(totRicevute.lordo));
    // Bollo: si versa per trimestre; nel mese che chiude il trimestre si mette l'importo
    if (d.mese.month() % 3 == 0) {
        const int trimestre = d.mese.month() / 3;
        qint64 bollo = 0;
        QString codice = QStringLiteral("252%1").arg(trimestre);
        for (const auto& v : d.bolli) {
            const auto r = v.toObject();
            if (r.value("trimestre").toInt() == trimestre && r.value("anno").toInt() == d.mese.year()) {
                bollo = r.value("importo").toInteger();
                codice = r.value("codice_tributo").toString();
            }
        }
        voce(QStringLiteral("Imposta di bollo %1° trimestre (codice tributo %2) €").arg(trimestre).arg(codice), e(bollo));
    }

    return {riepilogo, aliquote, giorni, incassi, chiusure, emesse, ricevute};
}

// ---------------------------------------------------------------------------

DialogoCommercialista::DialogoCommercialista(ClientServer& client, QWidget* parent)
    : QDialog(parent)
    , m_client(client)
{
    setWindowTitle(QStringLiteral("Riepilogo per il commercialista"));
    setObjectName("dialogo_commercialista");
    resize(620, 600);
    auto* layout = new QVBoxLayout(this);
    auto* barra = new QHBoxLayout;
    barra->addWidget(new QLabel(QStringLiteral("Mese"), this));
    m_mese = new QDateEdit(this);
    m_mese->setObjectName("commercialista_mese");
    m_mese->setDisplayFormat(QStringLiteral("MMMM yyyy"));
    m_mese->setCalendarPopup(true);
    const QDate oggi = QDate::currentDate();
    m_mese->setDate(QDate(oggi.year(), oggi.month(), 1).addMonths(-1));   // di solito si manda il mese finito
    m_mese->setMaximumDate(oggi);
    barra->addWidget(m_mese);
    barra->addStretch();
    layout->addLayout(barra);

    m_anteprima = new QTableWidget(0, 2, this);
    m_anteprima->setObjectName("commercialista_anteprima");
    m_anteprima->setHorizontalHeaderLabels({QStringLiteral("Voce"), QStringLiteral("Valore")});
    m_anteprima->verticalHeader()->hide();
    m_anteprima->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_anteprima->setSelectionMode(QAbstractItemView::NoSelection);
    m_anteprima->setShowGrid(false);
    m_anteprima->setAlternatingRowColors(true);
    m_anteprima->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_anteprima->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    layout->addWidget(m_anteprima, 1);

    m_stato = nota(QString(), this);
    m_stato->setObjectName("commercialista_stato");
    layout->addWidget(m_stato);

    auto* pulsanti = new QDialogButtonBox(this);
    m_salva = pulsanti->addButton(QStringLiteral("Salva Excel…"), QDialogButtonBox::AcceptRole);
    m_salva->setObjectName("commercialista_salva");
    m_salva->setDefault(true);
    m_salva->setEnabled(false);
    pulsanti->addButton(QDialogButtonBox::Close);
    layout->addWidget(pulsanti);

    connect(m_mese, &QDateEdit::dateChanged, this, &DialogoCommercialista::carica);
    connect(m_salva, &QPushButton::clicked, this, &DialogoCommercialista::salva);
    connect(pulsanti, &QDialogButtonBox::rejected, this, &QDialog::reject);
    carica();
}

void DialogoCommercialista::carica()
{
    const QDate primo(m_mese->date().year(), m_mese->date().month(), 1);
    const QDate ultimo = primo.addMonths(1).addDays(-1);
    m_dati = DatiMese{};
    m_dati.mese = primo;
    m_pronto = false;
    m_salva->setEnabled(false);
    m_stato->setText(QStringLiteral("Lettura dal server…"));
    const int richiesta = ++m_richiesta;

    const QString dal = primo.toString(Qt::ISODate), al = ultimo.toString(Qt::ISODate);
    const QString inizio = ClientServer::valoreFiltro(QDateTime(primo, QTime(0, 0)).toUTC().toString(Qt::ISODate));
    const QString fine = ClientServer::valoreFiltro(QDateTime(ultimo.addDays(1), QTime(0, 0)).toUTC().toString(Qt::ISODate));
    const QList<QPair<QString, QJsonArray DatiMese::*>> letture{
        {QStringLiteral("v_venduto_reparto_iva?giorno=gte.%1&giorno=lte.%2&order=giorno").arg(dal, al), &DatiMese::venduto},
        {QStringLiteral("v_incassi?giorno=gte.%1&giorno=lte.%2&order=giorno,terminale_id,tipo_pagamento").arg(dal, al), &DatiMese::incassi},
        {QStringLiteral("v_riconciliazione_chiusure?data_ora=gte.%1&data_ora=lt.%2&order=data_ora").arg(inizio, fine), &DatiMese::chiusure},
        {QStringLiteral("v_fatture?data=gte.%1&data=lte.%2&order=data,numero").arg(dal, al), &DatiMese::fatture},
        {QStringLiteral("fatture_passive?select=numero,data,tipo_documento,fornitore_nome,fornitore_partita_iva,imponibile,imposta,totale"
                        "&data=gte.%1&data=lte.%2&order=data").arg(dal, al), &DatiMese::passive},
        {QStringLiteral("v_bolli_trimestrali?anno=eq.%1").arg(primo.year()), &DatiMese::bolli},
    };
    auto mancanti = std::make_shared<int>(int(letture.size()));
    auto errore = std::make_shared<QString>();
    QPointer<DialogoCommercialista> io(this);
    for (const auto& [risorsa, campo] : letture) {
        m_client.leggi(risorsa, [io, richiesta, campo, mancanti, errore](const RispostaServer& r) {
            if (!io || richiesta != io->m_richiesta)
                return;
            if (r.ok())
                io->m_dati.*campo = r.json().array();
            else if (errore->isEmpty())
                *errore = r.errore;
            if (--*mancanti > 0)
                return;
            if (!errore->isEmpty()) {
                io->m_stato->setText(QStringLiteral("Non letto dal server: %1").arg(*errore));
                return;
            }
            io->m_pronto = true;
            io->mostra();
        });
    }
}

void DialogoCommercialista::mostra()
{
    const auto fogli = fogliCommercialista(m_dati);
    const auto& riepilogo = fogli.first();
    m_anteprima->setRowCount(int(riepilogo.righe.size()));
    for (int i = 0; i < riepilogo.righe.size(); ++i) {
        m_anteprima->setItem(i, 0, cella(riepilogo.righe[i].value(0)));
        m_anteprima->setItem(i, 1, cella(riepilogo.righe[i].value(1), true));
    }
    m_stato->setText(QStringLiteral("Nel file anche: corrispettivi per aliquota e per giorno, incassi, chiusure Z, "
                                    "fatture emesse e ricevute, una riga per documento."));
    m_salva->setEnabled(true);
}

void DialogoCommercialista::salva()
{
    if (!m_pronto)
        return;
    const QString nome = QStringLiteral("commercialista-%1.xlsx").arg(m_dati.mese.toString(QStringLiteral("yyyy-MM")));
    const QString file = QFileDialog::getSaveFileName(
        this, QStringLiteral("Salva il riepilogo per il commercialista"),
        QDir(QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation)).filePath(nome),
        QStringLiteral("Excel (*.xlsx)"));
    if (file.isEmpty())
        return;
    QFile f(file);
    if (!f.open(QIODevice::WriteOnly) || f.write(scriviXlsx(fogliCommercialista(m_dati))) < 0) {
        QMessageBox::warning(this, windowTitle(), QStringLiteral("File non salvato: %1").arg(f.errorString()));
        return;
    }
    m_stato->setText(QStringLiteral("Salvato: %1").arg(QDir::toNativeSeparators(file)));
}

} // namespace mdr
