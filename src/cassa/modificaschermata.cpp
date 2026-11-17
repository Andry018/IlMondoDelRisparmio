#include "modificaschermata.h"

#include "pannellocontanti.h"
#include "vistacassa.h"

#include <QFontMetrics>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QRegularExpression>
#include <QTimer>

#include <algorithm>
#include <climits>

namespace mdr {

namespace {

constexpr int sogliaTrascinamento = 12;   // pixel prima che un tocco diventi un trascinamento
const QColor blu(21, 101, 192);
const QColor arancio(255, 143, 0);

QString nomeBreve(Pannello p)
{
    switch (p) {
    case Pannello::Scontrino: return QStringLiteral("Scontrino");
    case Pannello::Giornale: return QStringLiteral("Giornale");
    case Pannello::Tasti: return QStringLiteral("Reparti e rapidi");
    case Pannello::Tastierino: return QStringLiteral("Tastierino");
    case Pannello::Comandi: return QStringLiteral("Comandi");
    case Pannello::Contanti: return QStringLiteral("Contanti fissi");
    }
    return {};
}

std::optional<Pannello> pannelloDa(const QString& chiave)
{
    for (const auto p : tuttiIPannelli())
        if (chiavePannello(p) == chiave)
            return p;
    return std::nullopt;
}

QString nomeTaglio(int centesimi)
{
    return centesimi >= 100 ? QStringLiteral("€ %1").arg(centesimi / 100) : QStringLiteral("%1 cent").arg(centesimi);
}

bool trascinabile(ModificaSchermata::Tipo t)
{
    return t != ModificaSchermata::Tipo::Nascondi && t != ModificaSchermata::Tipo::Azione;
}

// Il punto viene prima del tasto r leggendo come un libro? Su una sola colonna conta solo l'altezza.
bool prima(QPoint p, const QRect& r, bool unaColonna)
{
    if (unaColonna || p.y() < r.top() || p.y() > r.bottom())
        return p.y() < r.center().y();
    return p.x() < r.center().x();
}

// Dove si inserisce un tasto fra gli altri (rettangoli in ordine di lettura): posizione e linea
std::pair<int, QRect> posizioneFra(QPoint p, const QList<QRect>& altri, bool unaColonna)
{
    for (int i = 0; i < altri.size(); ++i) {
        const QRect& r = altri[i];
        if (prima(p, r, unaColonna))
            return {i, unaColonna || r.width() > 2 * r.height() + 200
                           ? QRect(r.left(), r.top() - 5, r.width(), 5)
                           : QRect(r.left() - 5, r.top(), 5, r.height())};
    }
    if (altri.isEmpty())
        return {0, {}};
    const QRect& u = altri.last();
    return {int(altri.size()), unaColonna ? QRect(u.left(), u.bottom() + 1, u.width(), 5)
                                          : QRect(u.right() + 1, u.top(), 5, u.height())};
}

} // namespace

ModificaSchermata::ModificaSchermata(VistaCassa& cassa)
    : QWidget(&cassa)
    , m_cassa(cassa)
{
    setObjectName("modifica_schermata");
    setFocusPolicy(Qt::StrongFocus);   // tastiera e lettore barcode non arrivano al campo della cassa
    setMouseTracking(false);
}

QFont ModificaSchermata::carattere(int punti, bool grassetto) const
{
    QFont f = font();
    f.setPointSize(m_cassa.aspetto().punti(punti));
    f.setBold(grassetto);
    return f;
}

void ModificaSchermata::aggiorna()
{
    // La cassa si stringe sopra la barra (che cresce se i pezzi nascosti vanno a capo)
    int altezza = 0;
    elementiBarra(&altezza);
    if (altezza != m_altezzaRiservata) {
        m_altezzaRiservata = altezza;
        m_cassa.riservaBasso(altezza);
    }
    update();
    // I layout si sistemano al giro dopo dell'event loop: si ridisegna anche allora
    QTimer::singleShot(0, this, [this] { update(); });
}

QRect ModificaSchermata::visibile(const QWidget* w) const
{
    if (!w || !w->isVisible())
        return {};
    QRect r(w->mapTo(&m_cassa, QPoint(0, 0)), w->size());
    for (const QWidget* p = w->parentWidget(); p && p != &m_cassa; p = p->parentWidget())
        r &= QRect(p->mapTo(&m_cassa, QPoint(0, 0)), p->size());
    // Sotto la barra in basso non si tocca niente
    return r & QRect(0, 0, width(), barra().top());
}

int ModificaSchermata::colonnaIn(QPoint p) const
{
    int migliore = -1;
    int distanza = INT_MAX;
    for (int c = 0; c < AspettoCassa::numeroColonne; ++c) {
        const QRect r = visibile(m_cassa.colonnaSchermo(c));
        if (r.isEmpty())
            continue;
        const int d = p.x() < r.left() ? r.left() - p.x() : p.x() > r.right() ? p.x() - r.right() : 0;
        if (d < distanza) {
            distanza = d;
            migliore = c;
        }
    }
    return migliore;
}

QRect ModificaSchermata::barra() const
{
    int altezza = 0;
    elementiBarra(&altezza);
    return QRect(0, height() - altezza, width(), altezza);
}

QList<ModificaSchermata::Elemento> ModificaSchermata::elementiBarra(int* altezza) const
{
    const auto& a = m_cassa.aspetto();
    QList<Elemento> pezzi;
    for (const auto& posto : a.pannelli)
        if (!posto.visibile)
            pezzi << Elemento{Tipo::Pannello, chiavePannello(posto.pannello), {}, nomeBreve(posto.pannello), true};
    if (a.mostrato(Pannello::Comandi))
        for (const auto& c : tuttiIComandi())
            if (!a.comandi.contains(c.chiave))
                pezzi << Elemento{Tipo::Comando, c.chiave, {}, c.testo, true};
    if (a.mostrato(Pannello::Contanti))
        for (const int c : AspettoCassa::tagliPredefiniti())
            if (!a.tagliContanti.contains(c))
                pezzi << Elemento{Tipo::Taglio, QString::number(c), {}, nomeTaglio(c), true};

    // A destra i tre tasti; a sinistra i pezzi nascosti, su più righe se servono
    const QList<Elemento> azioni{{Tipo::Azione, QStringLiteral("impostazioni"), {}, QStringLiteral("Altre impostazioni…")},
                                 {Tipo::Azione, QStringLiteral("annulla"), {}, QStringLiteral("Annulla")},
                                 {Tipo::Azione, QStringLiteral("salva"), {}, QStringLiteral("Salva")}};
    const QFontMetrics fm(carattere(11, true));
    constexpr int margine = 8;
    constexpr int titolo = 26;
    constexpr int alto = 40;
    int largoAzioni = 0;
    for (const auto& z : azioni)
        largoAzioni += fm.horizontalAdvance(z.testo) + 32 + 6;
    const int fine = std::max(200, width() - largoAzioni - 2 * margine);
    int x = margine;
    int y = titolo;
    for (auto& e : pezzi) {
        const int w = fm.horizontalAdvance(e.testo) + 28;
        if (x + w > fine && x > margine) {
            x = margine;
            y += alto + 6;
        }
        e.rett = QRect(x, y, w, alto);
        x += w + 6;
    }
    const int h = std::max(y + alto + margine, titolo + 48 + margine);
    if (altezza)
        *altezza = h;
    const int cima = height() - h;
    for (auto& e : pezzi)
        e.rett.translate(0, cima);
    int xa = width() - margine - largoAzioni + 6;
    QList<Elemento> tutti = pezzi;
    for (auto z : azioni) {
        const int w = fm.horizontalAdvance(z.testo) + 32;
        z.rett = QRect(xa, cima + (h - 48) / 2, w, 48);
        xa += w + 6;
        tutti << z;
    }
    return tutti;
}

QList<ModificaSchermata::Elemento> ModificaSchermata::elementi() const
{
    const auto& a = m_cassa.aspetto();
    QList<Elemento> v = elementiBarra();
    const QFontMetrics fm(carattere(11, true));

    // Maniglie dei pannelli e "✕" per toglierli
    QList<Elemento> maniglie;
    for (const auto& posto : a.pannelli) {
        if (!posto.visibile)
            continue;
        const QRect r = visibile(m_cassa.pannello(posto.pannello));
        if (r.width() < 80 || r.height() < 10)
            continue;
        // Nello spazio libero sopra il pannello (VistaCassa::disponi lo lascia in modifica); la ✕ accanto
        const QString chiave = chiavePannello(posto.pannello);
        const QString testo = QStringLiteral("✥  %1").arg(nomeBreve(posto.pannello));
        const int cima = r.top() - spazioManiglia + 3;
        const QRect maniglia(r.left() + 2, cima, std::min(r.width() - 44, fm.horizontalAdvance(testo) + 24), 32);
        if (posto.pannello != Pannello::Scontrino)
            v << Elemento{Tipo::Nascondi, chiave, QRect(maniglia.right() + 6, cima, 32, 32), QStringLiteral("✕")};
        maniglie << Elemento{Tipo::Pannello, chiave, maniglia, testo};
    }
    v += maniglie;

    // Bordi fra colonne vicine (in modifica le colonne si vedono tutte)
    QList<int> colonne;
    for (int c = 0; c < AspettoCassa::numeroColonne; ++c)
        if (!visibile(m_cassa.colonnaSchermo(c)).isEmpty())
            colonne << c;
    for (int i = 0; i + 1 < colonne.size(); ++i) {
        const QRect s = visibile(m_cassa.colonnaSchermo(colonne[i]));
        const QRect d = visibile(m_cassa.colonnaSchermo(colonne[i + 1]));
        const int x = (s.right() + d.left()) / 2;
        v << Elemento{Tipo::Bordo, QString::number(colonne[i]), QRect(x - 11, s.top(), 22, s.height()), QStringLiteral("⇔")};
    }

    // Tasti che si riordinano
    for (const auto& k : a.comandi)
        if (const QRect r = visibile(m_cassa.tastoComando(k)); !r.isEmpty())
            v << Elemento{Tipo::Comando, k, r, comando(k) ? comando(k)->testo : k};
    if (a.mostrato(Pannello::Contanti))
        for (const int c : a.tagliContanti)
            if (const QRect r = visibile(m_cassa.contanti()->tastoTaglio(c)); !r.isEmpty())
                v << Elemento{Tipo::Taglio, QString::number(c), r, nomeTaglio(c)};
    static const QRegularExpression reparto(QStringLiteral("^reparto_(\\d+)$"));
    static const QRegularExpression rapido(QStringLiteral("^rapido_(\\d+)$"));
    for (auto* b : m_cassa.findChildren<QPushButton*>()) {
        const QRect r = visibile(b);
        if (r.isEmpty())
            continue;
        if (const auto m = reparto.match(b->objectName()); m.hasMatch())
            v << Elemento{Tipo::Reparto, m.captured(1), r, b->text().replace(QLatin1Char('\n'), QLatin1Char(' '))};
        else if (const auto m2 = rapido.match(b->objectName()); m2.hasMatch())
            v << Elemento{Tipo::Rapido, m2.captured(1), r, b->text().replace(QLatin1Char('\n'), QLatin1Char(' '))};
    }
    return v;
}

std::optional<ModificaSchermata::Elemento> ModificaSchermata::elementoIn(QPoint p) const
{
    const bool inBarra = barra().contains(p);
    for (const auto& e : elementi()) {
        const bool diBarra = e.nascosto || e.tipo == Tipo::Azione;
        if (diBarra == inBarra && e.rett.contains(p))
            return e;
    }
    return std::nullopt;
}

QRect ModificaSchermata::rettangolo(Tipo tipo, const QString& chiave, bool nascosto) const
{
    for (const auto& e : elementi())
        if (e.tipo == tipo && e.chiave == chiave && e.nascosto == nascosto)
            return e.rett;
    return {};
}

std::optional<ModificaSchermata::Destinazione> ModificaSchermata::destinazione(const Elemento& e, QPoint p) const
{
    AspettoCassa a = m_cassa.aspetto();
    const bool inBarra = barra().contains(p);
    switch (e.tipo) {
    case Tipo::Pannello: {
        const auto pan = pannelloDa(e.chiave);
        if (!pan)
            return std::nullopt;
        if (inBarra) {
            if (e.nascosto || *pan == Pannello::Scontrino)
                return std::nullopt;   // lo scontrino (col campo del lettore) non si toglie
            a.mostra(*pan, false);
            return Destinazione{a, barra()};
        }
        const int c = colonnaIn(p);
        if (c < 0)
            return std::nullopt;
        const QRect rc = visibile(m_cassa.colonnaSchermo(c));
        int posizione = 0;
        int linea = rc.top() + 2;
        for (const auto& posto : a.pannelli) {
            if (posto.colonna != c || !posto.visibile || posto.pannello == *pan)
                continue;
            const QRect r = visibile(m_cassa.pannello(posto.pannello));
            if (r.isEmpty())
                continue;
            if (p.y() < r.center().y()) {
                linea = r.top() - 2;
                break;
            }
            ++posizione;
            linea = r.bottom() + 2;
        }
        a.spostaPannello(*pan, c, posizione);
        return Destinazione{a, QRect(rc.left(), linea - 3, rc.width(), 6)};
    }
    case Tipo::Comando: {
        if (inBarra) {
            if (e.nascosto)
                return std::nullopt;
            a.togliComando(e.chiave);
            return Destinazione{a, barra()};
        }
        if (!a.mostrato(Pannello::Comandi) || !visibile(m_cassa.pannello(Pannello::Comandi)).contains(p))
            return std::nullopt;
        QList<QRect> altri;
        for (const auto& k : std::as_const(a.comandi))
            if (k != e.chiave)
                altri << visibile(m_cassa.tastoComando(k));
        const auto [posizione, linea] = posizioneFra(p, altri, a.colonneComandi == 1);
        a.spostaComando(e.chiave, posizione);
        return Destinazione{a, linea};
    }
    case Tipo::Taglio: {
        const int c = e.chiave.toInt();
        if (inBarra) {
            if (e.nascosto)
                return std::nullopt;
            a.togliTaglio(c);
            return Destinazione{a, barra()};
        }
        if (!a.mostrato(Pannello::Contanti) || !visibile(m_cassa.pannello(Pannello::Contanti)).contains(p))
            return std::nullopt;
        // Banconote e monete stanno in due griglie: ci si sposta fra quelle dello stesso tipo
        const bool banconota = c >= 500;
        QList<int> stessi;
        QList<QRect> altri;
        for (const int x : std::as_const(a.tagliContanti))
            if (x != c && (x >= 500) == banconota) {
                stessi << x;
                altri << visibile(m_cassa.contanti()->tastoTaglio(x));
            }
        auto [posizione, linea] = posizioneFra(p, altri, false);
        // Posizione nell'elenco completo: davanti al taglio dello stesso tipo, o dopo l'ultimo
        int indice;
        if (posizione < stessi.size()) {
            a.togliTaglio(c);
            indice = int(a.tagliContanti.indexOf(stessi[posizione]));
        } else {
            a.togliTaglio(c);
            indice = stessi.isEmpty() ? (banconota ? 0 : int(a.tagliContanti.size()))
                                      : int(a.tagliContanti.indexOf(stessi.last())) + 1;
        }
        a.spostaTaglio(c, indice);
        if (linea.isNull())
            linea = visibile(m_cassa.pannello(Pannello::Contanti));
        return Destinazione{a, linea};
    }
    case Tipo::Reparto: {
        // Sopra un altro reparto: si scambiano di posto (l'elenco diventa quello mostrato)
        static const QRegularExpression nome(QStringLiteral("^reparto_(\\d+)$"));
        QList<std::pair<QRect, int>> mostrati;
        for (auto* b : m_cassa.findChildren<QPushButton*>())
            if (const auto m = nome.match(b->objectName()); m.hasMatch())
                if (const QRect r = visibile(b); !r.isEmpty())
                    mostrati.append({r, m.captured(1).toInt()});
        std::sort(mostrati.begin(), mostrati.end(), [](const auto& x, const auto& y) {
            return x.first.top() != y.first.top() ? x.first.top() < y.first.top() : x.first.left() < y.first.left();
        });
        QList<int> numeri;
        int verso = -1;
        QRect bersaglio;
        for (const auto& [r, n] : mostrati) {
            if (r.contains(p) && n != e.chiave.toInt()) {
                verso = int(numeri.size());
                bersaglio = r;
            }
            numeri << n;
        }
        const int da = int(numeri.indexOf(e.chiave.toInt()));
        if (verso < 0 || da < 0)
            return std::nullopt;
        numeri.move(da, verso);
        a.reparti = numeri;
        return Destinazione{a, bersaglio};
    }
    case Tipo::Rapido: {
        static const QRegularExpression nome(QStringLiteral("^rapido_(\\d+)$"));
        for (auto* b : m_cassa.findChildren<QPushButton*>())
            if (const auto m = nome.match(b->objectName()); m.hasMatch() && m.captured(1) != e.chiave)
                if (const QRect r = visibile(b); r.contains(p)) {
                    a.spostaTastoRapido(e.chiave.toInt(), m.captured(1).toInt());
                    return Destinazione{a, r};
                }
        return std::nullopt;
    }
    case Tipo::Bordo: {
        // Si sposta il bordo fra la colonna "chiave" e la successiva con qualcosa dentro
        const int s = e.chiave.toInt();
        std::array<int, AspettoCassa::numeroColonne> pixel{};
        for (int c = 0; c < AspettoCassa::numeroColonne; ++c) {
            const bool piena = std::any_of(a.pannelli.cbegin(), a.pannelli.cend(), [c](const PostoPannello& x) {
                return x.colonna == c && x.visibile;
            });
            pixel[c] = piena ? visibile(m_cassa.colonnaSchermo(c)).width() : 0;
        }
        int d = s + 1;
        while (d < AspettoCassa::numeroColonne && pixel[d] == 0)
            ++d;
        if (pixel[s] == 0 || d >= AspettoCassa::numeroColonne)
            return std::nullopt;
        const int spostamento = std::clamp(p.x() - m_inizio.x(), 60 - pixel[s], pixel[d] - 60);
        pixel[s] += spostamento;
        pixel[d] -= spostamento;
        a.larghezzeDaPixel(pixel);
        return Destinazione{a, QRect(e.rett.center().x() + spostamento - 2, e.rett.top(), 5, e.rett.height())};
    }
    case Tipo::Nascondi:
    case Tipo::Azione:
        break;
    }
    return std::nullopt;
}

std::optional<AspettoCassa> ModificaSchermata::tocco(const Elemento& e) const
{
    AspettoCassa a = m_cassa.aspetto();
    if (e.tipo == Tipo::Nascondi) {
        if (const auto pan = pannelloDa(e.chiave)) {
            a.mostra(*pan, false);
            return a;
        }
    }
    if (!e.nascosto)
        return std::nullopt;
    // Un tocco su un pezzo nascosto lo rimette dove sta di solito
    switch (e.tipo) {
    case Tipo::Pannello:
        if (const auto pan = pannelloDa(e.chiave)) {
            int colonna = a.colonna(*pan);
            for (const auto& d : AspettoCassa::disposizionePredefinita())
                if (d.pannello == *pan)
                    colonna = d.colonna;
            a.spostaPannello(*pan, colonna, 99);
            return a;
        }
        break;
    case Tipo::Comando: {
        const int altro = int(a.comandi.indexOf(QStringLiteral("altro")));
        a.spostaComando(e.chiave, altro >= 0 ? altro : int(a.comandi.size()));
        return a;
    }
    case Tipo::Taglio: {
        const int c = e.chiave.toInt();
        const int posizione = int(std::count_if(a.tagliContanti.cbegin(), a.tagliContanti.cend(), [c](int x) { return x > c; }));
        a.spostaTaglio(c, posizione);
        return a;
    }
    default:
        break;
    }
    return std::nullopt;
}

void ModificaSchermata::premi(QPoint p)
{
    m_preso = elementoIn(p);
    m_inizio = m_punto = p;
    m_trascinando = false;
    update();
}

void ModificaSchermata::sposta(QPoint p)
{
    if (!m_preso)
        return;
    m_punto = p;
    if (!m_trascinando && trascinabile(m_preso->tipo) && (p - m_inizio).manhattanLength() >= sogliaTrascinamento)
        m_trascinando = true;
    update();
}

void ModificaSchermata::rilascia(QPoint p)
{
    if (!m_preso)
        return;
    const Elemento e = *m_preso;
    const bool trascinato = m_trascinando;
    m_preso.reset();
    m_trascinando = false;
    update();
    if (trascinato) {
        if (const auto d = destinazione(e, p))
            m_cassa.cambiaAspetto(d->aspetto);
        return;
    }
    if (e.tipo == Tipo::Azione) {
        if (e.chiave == QLatin1String("impostazioni"))
            emit m_cassa.altreImpostazioni();
        else
            emit m_cassa.modificaFinita(e.chiave == QLatin1String("salva"));
        return;
    }
    if (const auto a = tocco(e))
        m_cassa.cambiaAspetto(*a);
}

void ModificaSchermata::trascina(QPoint da, QPoint a)
{
    premi(da);
    sposta((da + a) / 2);
    sposta(a);
    rilascia(a);
}

void ModificaSchermata::tocca(QPoint p)
{
    premi(p);
    rilascia(p);
}

void ModificaSchermata::mousePressEvent(QMouseEvent* e)
{
    if (e->button() == Qt::LeftButton)
        premi(e->position().toPoint());
}

void ModificaSchermata::mouseMoveEvent(QMouseEvent* e)
{
    sposta(e->position().toPoint());
}

void ModificaSchermata::mouseReleaseEvent(QMouseEvent* e)
{
    if (e->button() == Qt::LeftButton)
        rilascia(e->position().toPoint());
}

void ModificaSchermata::paintEvent(QPaintEvent*)
{
    QPainter g(this);
    g.setRenderHint(QPainter::Antialiasing);
    const auto& a = m_cassa.aspetto();
    const QRect b = barra();
    g.fillRect(QRect(0, 0, width(), b.top()), QColor(21, 101, 192, 30));

    // Colonne vuote: dove si può lasciare un pannello
    g.setFont(carattere(11, true));
    for (int c = 0; c < AspettoCassa::numeroColonne; ++c) {
        const bool vuota = std::none_of(a.pannelli.cbegin(), a.pannelli.cend(), [c](const PostoPannello& x) {
            return x.colonna == c && x.visibile;
        });
        const QRect r = visibile(m_cassa.colonnaSchermo(c));
        if (!vuota || r.isEmpty())
            continue;
        g.setPen(QPen(blu, 2, Qt::DashLine));
        g.setBrush(QColor(21, 101, 192, 25));
        g.drawRoundedRect(r.adjusted(4, 4, -4, -4), 8, 8);
        g.drawText(r, Qt::AlignCenter | Qt::TextWordWrap, QStringLiteral("Colonna vuota:\ntrascina qui\nun pannello"));
    }
    // Cornici dei pannelli
    for (const auto& posto : a.pannelli) {
        if (!posto.visibile)
            continue;
        const QRect r = visibile(m_cassa.pannello(posto.pannello));
        if (r.isEmpty())
            continue;
        g.setPen(QPen(blu, 2, Qt::DashLine));
        g.setBrush(Qt::NoBrush);
        g.drawRoundedRect(QRectF(r).adjusted(1, 1, -1, -1), 6, 6);
    }

    const auto tutti = elementi();
    for (const auto& e : tutti) {
        if (e.nascosto || e.tipo == Tipo::Azione)
            continue;
        switch (e.tipo) {
        case Tipo::Comando:
        case Tipo::Taglio:
        case Tipo::Reparto:
        case Tipo::Rapido:
            g.setPen(QPen(QColor(255, 255, 255, 220), 2, Qt::DotLine));
            g.setBrush(Qt::NoBrush);
            g.drawRoundedRect(QRectF(e.rett).adjusted(1, 1, -1, -1), 4, 4);
            break;
        case Tipo::Pannello:
            g.setPen(Qt::NoPen);
            g.setBrush(blu);
            g.drawRoundedRect(e.rett, 8, 8);
            g.setPen(Qt::white);
            g.setFont(carattere(11, true));
            g.drawText(e.rett, Qt::AlignCenter, e.testo);
            break;
        case Tipo::Nascondi:
            g.setPen(Qt::NoPen);
            g.setBrush(QColor(198, 40, 40));
            g.drawEllipse(e.rett);
            g.setPen(Qt::white);
            g.setFont(carattere(12, true));
            g.drawText(e.rett, Qt::AlignCenter, e.testo);
            break;
        case Tipo::Bordo: {
            g.setPen(Qt::NoPen);
            g.setBrush(QColor(21, 101, 192, 170));
            const QRect linea(e.rett.center().x() - 3, e.rett.top() + 6, 7, e.rett.height() - 12);
            g.drawRoundedRect(linea, 3, 3);
            const QRect presa(e.rett.center().x() - 18, e.rett.center().y() - 18, 36, 36);
            g.setBrush(blu);
            g.drawEllipse(presa);
            g.setPen(Qt::white);
            g.setFont(carattere(12, true));
            g.drawText(presa, Qt::AlignCenter, e.testo);
            break;
        }
        default:
            break;
        }
    }

    // Barra in basso: pezzi nascosti e tasti
    g.setPen(Qt::NoPen);
    g.setBrush(palette().window());
    g.drawRect(b);
    g.fillRect(QRect(b.left(), b.top(), b.width(), 3), blu);
    g.setPen(palette().windowText().color());
    g.setFont(carattere(10, true));
    g.drawText(QRect(b.left() + 10, b.top() + 4, b.width() - 20, 22), Qt::AlignLeft | Qt::AlignVCenter,
               QStringLiteral("Nascosti: trascina qui quello che vuoi togliere, tocca un pezzo per rimetterlo"));
    g.setFont(carattere(11, true));
    for (const auto& e : tutti) {
        if (!e.nascosto && e.tipo != Tipo::Azione)
            continue;
        QColor sfondo = e.tipo == Tipo::Pannello ? blu
                      : e.tipo == Tipo::Taglio   ? QColor(161, 117, 0)
                                                 : QColor(69, 90, 100);
        if (e.tipo == Tipo::Azione)
            sfondo = e.chiave == QLatin1String("salva")     ? QColor(46, 125, 50)
                   : e.chiave == QLatin1String("annulla") ? QColor(110, 110, 110)
                                                            : blu;
        g.setPen(Qt::NoPen);
        g.setBrush(sfondo);
        g.drawRoundedRect(e.rett, 8, 8);
        g.setPen(Qt::white);
        g.drawText(e.rett, Qt::AlignCenter, e.testo);
    }

    // Trascinamento: dove arriva e il pezzo sotto il dito
    if (m_trascinando && m_preso) {
        if (const auto d = destinazione(*m_preso, m_punto)) {
            if (d->indicatore == b) {
                g.setPen(QPen(arancio, 4));
                g.setBrush(Qt::NoBrush);
                g.drawRect(b.adjusted(2, 2, -2, -2));
            } else {
                g.setPen(Qt::NoPen);
                g.setBrush(arancio);
                g.drawRoundedRect(d->indicatore, 2, 2);
            }
        }
        if (m_preso->tipo != Tipo::Bordo) {
            const QFontMetrics fm(carattere(12, true));
            QRect fantasma(0, 0, fm.horizontalAdvance(m_preso->testo) + 28, 44);
            fantasma.moveCenter(m_punto - QPoint(0, 34));   // sopra il dito, che lo coprirebbe
            g.setPen(QPen(Qt::white, 2));
            g.setBrush(QColor(21, 101, 192, 225));
            g.drawRoundedRect(fantasma, 10, 10);
            g.setFont(carattere(12, true));
            g.drawText(fantasma, Qt::AlignCenter, m_preso->testo);
        }
    }
}

} // namespace mdr
