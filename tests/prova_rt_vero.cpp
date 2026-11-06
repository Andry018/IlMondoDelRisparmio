// Prova del driver Custom sul registratore VERO (K3-F RT), da usare solo in MODALITÀ ADDESTRAMENTO.
// Non è un test automatico (non sta in ctest): si lancia a mano dal PC collegato in rete al
// registratore e scrive ogni scambio XML in un file, per correggere il driver con le risposte vere.
//
//   mdr-prova-rt 192.168.1.50                           solo letture (informazioni e stato)
//   mdr-prova-rt 192.168.1.50 --addestramento           + vendite, reso, annullo, ristampa, cassetto
//   mdr-prova-rt 192.168.1.50 --addestramento --chiusura  + chiusura Z alla fine
//   opzioni: --aliquote "2200=1;1000=2"  --pagamenti "contanti=1;carta=2"
//            --cartella <dove scrivere il registro> (predefinita: Documenti\prova-rt)
//
// Senza --addestramento non si emette nessun documento: un registratore in modalità normale
// emetterebbe scontrini fiscali veri. La chiusura Z vuole anche --chiusura.
#include "fiscal/customxml.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QStandardPaths>
#include <QTextStream>
#include <QTime>
#include <QUuid>

#include <cstdio>

using namespace mdr;

namespace {

QTextStream& out()
{
    static QTextStream s(stdout);
    return s;
}

RigaCarrello riga(const QString& descrizione, const QString& reparto, int aliquota, const QString& natura,
                  Quantita q, int centesimi, int sconto = 0)
{
    return {QUuid::createUuid(), descrizione, reparto, {aliquota, natura}, q, Money::fromCents(centesimi),
            Money::fromCents(sconto)};
}

void paga(Carrello& c, TipoPagamento tipo)
{
    c.aggiungiPagamento({tipo, c.residuo()});
}

int passati = 0;
int falliti = 0;

void esito(const QString& passo, const EsitoFiscale& e)
{
    if (e.ok) {
        ++passati;
        out() << "OK      " << passo << "  (Z " << e.numeroAzzeramento << ", documento " << e.numeroDocumento << ")\n";
    } else {
        ++falliti;
        out() << "ERRORE  " << passo << ": " << e.errore << "\n";
    }
    out().flush();
}

} // namespace

int main(int argc, char* argv[])
{
    QCoreApplication app(argc, argv);
    const auto a = app.arguments();
    if (a.size() < 2 || a[1].startsWith(QLatin1String("--"))) {
        out() << "Uso: mdr-prova-rt <indirizzo[:porta]> [--addestramento] [--chiusura] [--aliquote ...] "
                 "[--pagamenti ...] [--cartella dir] [--forza]\n";
        return 2;
    }
    const QString indirizzo = a[1];
    const bool addestramento = a.contains(QStringLiteral("--addestramento"));
    const bool chiusura = a.contains(QStringLiteral("--chiusura"));
    auto opzione = [&a](const QString& nome) {
        const int i = a.indexOf(nome);
        return i > 0 && i + 1 < a.size() ? a[i + 1] : QString();
    };
    QString cartella = opzione(QStringLiteral("--cartella"));
    if (cartella.isEmpty())
        cartella = QDir(QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation)).filePath(QStringLiteral("prova-rt"));
    const auto conf = ConfigurazioneCustom::daTesto(opzione(QStringLiteral("--pagamenti")), opzione(QStringLiteral("--aliquote")));

    // Stesso registro della cassa: <cartella>/rt-AAAAMMGG.log, con ogni richiesta e risposta
    auto trasporto = std::make_unique<TrasportoConRegistro>(std::make_unique<TrasportoHttpCustom>(indirizzo), cartella, 365);
    auto* reg = trasporto.get();
    const QString registro = reg->fileDiOggi();
    reg->nota(QStringLiteral("Prova su %1, pagamenti \"%2\", aliquote \"%3\", addestramento=%4 chiusura=%5")
                  .arg(indirizzo, conf.testoPagamenti(), conf.testoAliquote())
                  .arg(addestramento)
                  .arg(chiusura));
    StampanteCustomXml rt(std::move(trasporto), conf);
    out() << "Registratore " << indirizzo << " — registro: " << QDir::toNativeSeparators(registro) << "\n\n";

    // 1. Sola lettura
    reg->nota(QStringLiteral("informazioni"));
    QString errore;
    if (!rt.leggiInformazioni(&errore)) {
        out() << "ERRORE  informazioni: " << errore << "\nIl registratore non risponde: controllare indirizzo, rete e servizio XML.\n";
        return 1;
    }
    out() << "OK      informazioni: matricola " << rt.matricola() << " — " << rt.descrizioneInformazioni() << "\n";
    reg->nota(QStringLiteral("stato"));
    esito(QStringLiteral("stato"), rt.stato());

    if (!addestramento) {
        out() << "\nSolo letture. Con il registratore in MODALITÀ ADDESTRAMENTO rilanciare con --addestramento.\n";
        return falliti ? 1 : 0;
    }
    // Sicurezza in più: getInfo dice se il registratore è in simulazione (addestramento). Se non lo
    // è ci si ferma: i documenti sarebbero fiscali veri. --forza solo se il tecnico conferma che
    // l'addestramento è attivo e il registratore non lo dice in getInfo.
    if (!rt.inAddestramento() && !a.contains(QStringLiteral("--forza"))) {
        out() << "\nFERMO: il registratore non risulta in addestramento (getInfo, \"simulation\").\n"
                 "Nessun documento emesso. Metterlo in addestramento col tecnico (o, se lui conferma, --forza).\n";
        return 1;
    }

    // 2. Vendita in contanti, una riga al 22%
    reg->nota(QStringLiteral("vendita contanti"));
    Carrello v1;
    v1.aggiungiRiga(riga(QStringLiteral("PROVA CONTANTI 22%"), QStringLiteral("1"), 2200, {}, Quantita::pezzi(1), 100));
    paga(v1, TipoPagamento::Contanti);
    const auto e1 = rt.emettiDocumentoCommerciale(v1);
    esito(QStringLiteral("vendita in contanti (1,00 al 22%)"), e1);

    // 3. Carta: più pezzi dello stesso articolo al 10%
    reg->nota(QStringLiteral("vendita carta, 3 pezzi"));
    Carrello v2;
    v2.aggiungiRiga(riga(QStringLiteral("PROVA 3 PEZZI 10%"), QStringLiteral("2"), 1000, {}, Quantita::pezzi(3), 120));
    paga(v2, TipoPagamento::Carta);
    const auto e2 = rt.emettiDocumentoCommerciale(v2);
    esito(QStringLiteral("vendita con carta (3 x 1,20 al 10%)"), e2);

    // 4. Aliquote diverse, peso, sconto di riga e sul totale, pagamento misto
    reg->nota(QStringLiteral("vendita aliquote miste, sconti, pagamento misto"));
    Carrello v3;
    v3.aggiungiRiga(riga(QStringLiteral("PROVA 22%"), QStringLiteral("1"), 2200, {}, Quantita::pezzi(1), 250, 20));
    v3.aggiungiRiga(riga(QStringLiteral("PROVA 10%"), QStringLiteral("2"), 1000, {}, Quantita::pezzi(2), 99));
    v3.aggiungiRiga(riga(QStringLiteral("PROVA 4% A PESO"), QStringLiteral("3"), 400, {}, Quantita::fromMillesimi(350), 200));
    v3.aggiungiRiga(riga(QStringLiteral("PROVA ESENTE"), QStringLiteral("4"), 0, QStringLiteral("N4"), Quantita::pezzi(1), 150));
    v3.aggiungiRiga(riga(QStringLiteral("PROVA 5%"), QStringLiteral("5"), 500, {}, Quantita::pezzi(1), 300));
    v3.scontoTotale(Money::fromCents(50));
    v3.aggiungiPagamento({TipoPagamento::Contanti, Money::fromCents(500)});
    paga(v3, TipoPagamento::Carta);
    const auto e3 = rt.emettiDocumentoCommerciale(v3);
    esito(QStringLiteral("vendita con 5 aliquote, sconti e pagamento misto (totale %1)").arg(v3.totale().toString()), e3);

    // 5. Codice fiscale del cliente (spesa sanitaria)
    reg->nota(QStringLiteral("vendita con codice fiscale"));
    Carrello v4;
    v4.aggiungiRiga(riga(QStringLiteral("PROVA DISPOSITIVO MEDICO"), QStringLiteral("1"), 2200, {}, Quantita::pezzi(1), 500));
    v4.impostaCodiceFiscale(QStringLiteral("RSSMRA80A01H501U"));
    paga(v4, TipoPagamento::Bancomat);
    const auto e4 = rt.emettiDocumentoCommerciale(v4);
    esito(QStringLiteral("vendita con codice fiscale, bancomat"), e4);

    // 6. Reso di una riga della vendita 4 (aliquote miste) e annullo della vendita 2
    if (e3.ok) {
        reg->nota(QStringLiteral("reso"));
        Carrello reso;
        reso.aggiungiRiga(v3.righe()[1]);   // i 2 pezzi al 10%
        paga(reso, TipoPagamento::Contanti);
        esito(QStringLiteral("reso di una riga della vendita con aliquote miste"),
              rt.reso(reso, {rt.matricola(), QDate::currentDate(), e3.numeroAzzeramento, e3.numeroDocumento}));
    }
    if (e2.ok) {
        reg->nota(QStringLiteral("annullo"));
        const RiferimentoDocumento rif{rt.matricola(), QDate::currentDate(), e2.numeroAzzeramento, e2.numeroDocumento};
        esito(QStringLiteral("annullo della vendita con carta"), rt.annullo(rif));
        reg->nota(QStringLiteral("annullo ripetuto (deve essere rifiutato)"));
        const auto doppio = rt.annullo(rif);
        out() << (doppio.ok ? "ATTENZIONE annullo ripetuto accettato\n" : "OK      annullo ripetuto rifiutato: ")
              << (doppio.ok ? QString() : doppio.errore + QStringLiteral("\n"));
    }

    // 7. Ristampa e cassetto
    reg->nota(QStringLiteral("ristampa"));
    esito(QStringLiteral("ristampa dell'ultimo documento"), rt.ristampaUltimoDocumento());
    reg->nota(QStringLiteral("cassetto"));
    esito(QStringLiteral("apertura cassetto"), rt.aperturaCassetto());

    // 8. Chiusura Z (solo se richiesta)
    if (chiusura) {
        reg->nota(QStringLiteral("chiusura Z"));
        const auto z = rt.chiusuraGiornaliera();
        esito(QStringLiteral("chiusura Z"), z);
        if (z.ok && z.totali.presenti) {
            out() << "        totali: corrispettivi " << z.totali.totale.toString() << ", vendite " << z.totali.vendite.toString()
                  << ", resi " << z.totali.resi.toString() << ", annulli " << z.totali.annulli.toString()
                  << ", documenti " << z.totali.documenti << "\n";
            for (auto it = z.totali.perAliquota.cbegin(); it != z.totali.perAliquota.cend(); ++it)
                out() << "        aliquota " << it.key() / 100.0 << "%: " << it.value().toString() << "\n";
        }
    } else {
        out() << "        chiusura Z non fatta (aggiungere --chiusura)\n";
    }

    out() << "\nRiepilogo: " << passati << " passi riusciti, " << falliti << " con errore. Registro: "
          << QDir::toNativeSeparators(registro) << "\n";
    return falliti ? 1 : 0;
}
