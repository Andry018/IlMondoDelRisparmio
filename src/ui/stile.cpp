#include "stile.h"

#include <QApplication>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPalette>
#include <QStyle>
#include <QStyleOptionTab>

#include <cmath>
#include <numbers>

namespace mdr {

namespace {

// Colori del negozio (provvisori, finché non arriva il logo): verde acqua del "risparmio".
// Cambiando questi tre cambiano cassa e gestione insieme.
const QColor primarioChiaro(0x0f, 0x76, 0x6e);
const QColor primarioScuroChiaro(0x0b, 0x5d, 0x57);
const QColor primarioTenueChiaro(0xd9, 0xf2, 0xee);

TemaStile g_tema = TemaStile::Chiaro;

QString n(const QColor& c)
{
    return c.name();
}

} // namespace

const ColoriStile& coloriStile(TemaStile tema)
{
    static const ColoriStile chiaro{
        primarioChiaro, primarioScuroChiaro, primarioTenueChiaro, Qt::white,
        QColor(0xf3, 0xf5, 0xf7), Qt::white, QColor(0xf7, 0xf9, 0xfa), QColor(0xd9, 0xdf, 0xe5),
        QColor(0x1f, 0x29, 0x33), QColor(0x5f, 0x6b, 0x7a),
        QColor(0x2e, 0x7d, 0x32), QColor(0xc6, 0x28, 0x28), QColor(0xb2, 0x6a, 0x00)};
    static const ColoriStile scuro{
        QColor(0x2d, 0xbd, 0xac), QColor(0x1f, 0x93, 0x86), QColor(0x17, 0x40, 0x3c), QColor(0x0c, 0x1f, 0x1d),
        QColor(0x1d, 0x21, 0x26), QColor(0x26, 0x2b, 0x31), QColor(0x2c, 0x32, 0x39), QColor(0x3b, 0x43, 0x4c),
        QColor(0xe6, 0xe9, 0xec), QColor(0x9b, 0xa6, 0xb1),
        QColor(0x66, 0xbb, 0x6a), QColor(0xef, 0x53, 0x50), QColor(0xff, 0xb7, 0x4d)};
    // Alto contrasto: nero e bianco pieni, selezione gialla (luce forte sul banco, vista stanca)
    static const ColoriStile contrasto{
        QColor(0xff, 0xd6, 0x00), QColor(0xff, 0xea, 0x70), QColor(0x4a, 0x40, 0x00), Qt::black,
        Qt::black, Qt::black, QColor(0x12, 0x12, 0x12), Qt::white,
        Qt::white, QColor(0xe0, 0xe0, 0xe0),
        QColor(0x7c, 0xff, 0x7c), QColor(0xff, 0x6e, 0x6e), QColor(0xff, 0xd6, 0x00)};
    switch (tema) {
    case TemaStile::Scuro: return scuro;
    case TemaStile::Contrasto: return contrasto;
    case TemaStile::Chiaro: break;
    }
    return chiaro;
}

const ColoriStile& coloriAttuali()
{
    return coloriStile(g_tema);
}

QString foglioDiStile(const ColoriStile& c)
{
    // Solo quello che Fusion non fa già bene con la palette: tasti, campi, tabelle, riquadri,
    // linguette, barre di scorrimento. Combo e spin restano a Fusion (con il bordo in QSS perderebbero
    // le frecce). I tasti colorati della cassa hanno il loro stile, che vince su questo.
    return QStringLiteral(R"(
QMainWindow, QDialog { background: %1; }
QPushButton {
    background: %2; color: %3; border: 1px solid %4; border-radius: 6px; padding: 5px 8px;
}
QPushButton:hover { border-color: %5; }
QPushButton:pressed, QPushButton:checked { background: %6; border-color: %5; }
QPushButton:disabled { color: %7; background: %8; border-color: %4; }
QPushButton:default { background: %5; color: %9; border-color: %10; font-weight: 600; }
QPushButton:default:pressed { background: %10; }
QPushButton:default:disabled { color: %7; background: %8; border-color: %4; }
QLineEdit, QPlainTextEdit, QTextEdit {
    background: %2; color: %3; border: 1px solid %4; border-radius: 5px; padding: 4px 6px;
    selection-background-color: %5; selection-color: %9;
}
QLineEdit:focus, QPlainTextEdit:focus, QTextEdit:focus { border: 1px solid %5; }
QLineEdit:disabled { color: %7; background: %8; }
QTableView, QTableWidget, QListView, QListWidget, QTreeView {
    background: %2; color: %3; border: 1px solid %4; border-radius: 6px; gridline-color: %8;
    selection-background-color: %6; selection-color: %3; alternate-background-color: %8;
}
QHeaderView::section {
    background: %8; color: %7; border: none; border-bottom: 1px solid %4; border-right: 1px solid %4;
    padding: 5px 6px; font-weight: 600;
}
QGroupBox {
    background: %2; border: 1px solid %4; border-radius: 8px; margin-top: 16px; padding-top: 6px;
}
QGroupBox::title {
    subcontrol-origin: margin; subcontrol-position: top left; left: 10px; padding: 0 4px;
    color: %7; font-weight: 600;
}
QTabWidget::pane { border: none; background: %1; }
QTabBar::tab {
    background: transparent; color: %7; padding: 7px 14px; border: none; border-bottom: 2px solid transparent;
}
QTabBar::tab:selected { color: %5; border-bottom: 2px solid %5; font-weight: 600; }
QTabBar::tab:hover:!selected { color: %3; }
QTabBar::tab:disabled { color: %4; }
QStatusBar { background: %8; border-top: 1px solid %4; color: %7; }
QToolTip { background: %3; color: %2; border: none; padding: 4px 6px; }
QScrollBar:vertical { background: transparent; width: 12px; margin: 2px; }
QScrollBar:horizontal { background: transparent; height: 12px; margin: 2px; }
QScrollBar::handle:vertical { background: %4; border-radius: 4px; min-height: 30px; }
QScrollBar::handle:horizontal { background: %4; border-radius: 4px; min-width: 30px; }
QScrollBar::handle:hover { background: %7; }
QScrollBar::add-line, QScrollBar::sub-line { width: 0; height: 0; }
QScrollBar::add-page, QScrollBar::sub-page { background: transparent; }
QCheckBox, QRadioButton, QLabel { color: %3; }
QLabel[ruolo="nota"] { color: %7; }
)")
        .arg(n(c.sfondo), n(c.superficie), n(c.testo), n(c.bordo), n(c.primario), n(c.primarioTenue),
             n(c.testoSecondario), n(c.superficieAlta), n(c.testoSuPrimario))
        .arg(n(c.primarioScuro));
}

void applicaStile(TemaStile tema)
{
    g_tema = tema;
    const auto& c = coloriStile(tema);
    QPalette p = QApplication::style()->standardPalette();
    p.setColor(QPalette::Window, c.sfondo);
    p.setColor(QPalette::WindowText, c.testo);
    p.setColor(QPalette::Base, c.superficie);
    p.setColor(QPalette::AlternateBase, c.superficieAlta);
    p.setColor(QPalette::Text, c.testo);
    p.setColor(QPalette::Button, c.superficie);
    p.setColor(QPalette::ButtonText, c.testo);
    p.setColor(QPalette::ToolTipBase, c.testo);
    p.setColor(QPalette::ToolTipText, c.superficie);
    p.setColor(QPalette::PlaceholderText, c.testoSecondario);
    p.setColor(QPalette::Highlight, c.primario);
    p.setColor(QPalette::HighlightedText, c.testoSuPrimario);
    p.setColor(QPalette::Link, c.primario);
    p.setColor(QPalette::Mid, c.bordo);
    p.setColor(QPalette::Light, c.superficie);
    p.setColor(QPalette::Dark, c.bordo);
    for (auto ruolo : {QPalette::Text, QPalette::ButtonText, QPalette::WindowText})
        p.setColor(QPalette::Disabled, ruolo, c.testoSecondario);
    QApplication::setPalette(p);
    qApp->setStyleSheet(foglioDiStile(c));
}

// ---------------------------------------------------------------------------
// Icone
// ---------------------------------------------------------------------------

QIcon iconaMenu(const QString& nome, const QColor& colore, int lato)
{
    QPixmap px(lato, lato);
    px.fill(Qt::transparent);
    QPainter p(&px);
    p.setRenderHint(QPainter::Antialiasing);
    p.scale(lato / 64.0, lato / 64.0);   // si disegna su una griglia di 64
    QPen penna(colore, 5, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
    p.setPen(penna);
    p.setBrush(Qt::NoBrush);
    auto pieno = [&](const QRectF& r) {
        p.setBrush(colore);
        p.drawEllipse(r);
        p.setBrush(Qt::NoBrush);
    };

    if (nome == QLatin1String("cruscotto")) {
        p.drawLine(10, 54, 54, 54);
        p.drawLine(18, 46, 18, 34);
        p.drawLine(30, 46, 30, 20);
        p.drawLine(42, 46, 42, 28);
        p.drawLine(52, 46, 52, 12);
    } else if (nome == QLatin1String("articoli")) {
        QPainterPath t;
        t.moveTo(10, 32); t.lineTo(30, 10); t.lineTo(54, 10); t.lineTo(54, 34); t.lineTo(32, 56); t.closeSubpath();
        p.drawPath(t);
        pieno(QRectF(40, 18, 7, 7));
    } else if (nome == QLatin1String("magazzino")) {
        p.drawRoundedRect(QRectF(10, 22, 44, 32), 3, 3);
        p.drawLine(6, 22, 58, 22);
        p.drawLine(14, 12, 50, 12);
        p.drawLine(14, 12, 6, 22);
        p.drawLine(50, 12, 58, 22);
        p.drawLine(26, 34, 38, 34);
    } else if (nome == QLatin1String("ordini")) {
        p.drawRoundedRect(QRectF(14, 12, 36, 44), 4, 4);
        p.drawRoundedRect(QRectF(24, 7, 16, 9), 2, 2);
        p.drawLine(22, 28, 42, 28);
        p.drawLine(22, 37, 42, 37);
        p.drawLine(22, 46, 34, 46);
    } else if (nome == QLatin1String("etichette")) {
        p.drawRoundedRect(QRectF(8, 14, 48, 36), 4, 4);
        p.setPen(QPen(colore, 3, Qt::SolidLine, Qt::FlatCap));
        for (const int x : {16, 22, 25, 31, 36, 39, 45, 48})
            p.drawLine(x, 22, x, 42);
    } else if (nome == QLatin1String("promozioni")) {
        p.drawLine(16, 50, 48, 14);
        p.drawEllipse(QRectF(12, 12, 13, 13));
        p.drawEllipse(QRectF(39, 39, 13, 13));
    } else if (nome == QLatin1String("vendite")) {
        QPainterPath s;
        s.moveTo(14, 8); s.lineTo(50, 8); s.lineTo(50, 56);
        s.lineTo(44, 51); s.lineTo(38, 56); s.lineTo(32, 51); s.lineTo(26, 56); s.lineTo(20, 51); s.lineTo(14, 56);
        s.closeSubpath();
        p.drawPath(s);
        p.drawLine(22, 22, 42, 22);
        p.drawLine(22, 32, 42, 32);
        p.drawLine(22, 42, 34, 42);
    } else if (nome == QLatin1String("fatture")) {
        QPainterPath d;
        d.moveTo(14, 8); d.lineTo(38, 8); d.lineTo(50, 20); d.lineTo(50, 56); d.lineTo(14, 56); d.closeSubpath();
        p.drawPath(d);
        p.drawLine(38, 8, 38, 20);
        p.drawLine(38, 20, 50, 20);
        p.drawLine(22, 34, 42, 34);
        p.drawLine(22, 44, 42, 44);
    } else if (nome == QLatin1String("clienti")) {
        p.drawEllipse(QRectF(22, 8, 20, 20));
        p.drawArc(QRectF(10, 34, 44, 36), 0, 180 * 16);
    } else if (nome == QLatin1String("fornitori")) {
        p.drawRoundedRect(QRectF(6, 16, 32, 28), 2, 2);
        QPainterPath cab;
        cab.moveTo(38, 24); cab.lineTo(50, 24); cab.lineTo(58, 34); cab.lineTo(58, 44); cab.lineTo(38, 44);
        p.drawPath(cab);
        p.drawEllipse(QRectF(12, 42, 11, 11));
        p.drawEllipse(QRectF(42, 42, 11, 11));
    } else if (nome == QLatin1String("chiusure")) {
        p.drawRoundedRect(QRectF(10, 10, 44, 44), 8, 8);
        QPainterPath z;
        z.moveTo(22, 22); z.lineTo(42, 22); z.lineTo(22, 42); z.lineTo(42, 42);
        p.drawPath(z);
    } else if (nome == QLatin1String("resi")) {
        QPainterPath a;
        a.moveTo(50, 46);
        a.cubicTo(50, 26, 40, 22, 18, 22);
        p.drawPath(a);
        p.drawLine(18, 22, 28, 12);
        p.drawLine(18, 22, 28, 32);
    } else if (nome == QLatin1String("operatori")) {
        p.drawEllipse(QRectF(10, 12, 16, 16));
        p.drawArc(QRectF(2, 34, 32, 30), 0, 180 * 16);
        p.drawEllipse(QRectF(38, 12, 16, 16));
        p.drawArc(QRectF(30, 34, 32, 30), 0, 180 * 16);
    } else if (nome == QLatin1String("negozio")) {
        p.drawRect(QRectF(12, 28, 40, 26));
        QPainterPath tenda;
        tenda.moveTo(8, 28); tenda.lineTo(14, 12); tenda.lineTo(50, 12); tenda.lineTo(56, 28);
        p.drawPath(tenda);
        p.drawLine(8, 28, 56, 28);
        p.drawRect(QRectF(26, 38, 12, 16));
    } else if (nome == QLatin1String("palmare")) {
        p.drawRoundedRect(QRectF(18, 6, 28, 52), 5, 5);
        p.drawLine(28, 49, 36, 49);
        p.drawLine(24, 16, 40, 16);
    } else if (nome == QLatin1String("impostazioni")) {
        for (int i = 0; i < 8; ++i) {
            const double a = i * std::numbers::pi / 4;
            p.drawLine(QPointF(32 + 16 * std::cos(a), 32 + 16 * std::sin(a)), QPointF(32 + 24 * std::cos(a), 32 + 24 * std::sin(a)));
        }
        p.drawEllipse(QRectF(16, 16, 32, 32));
        p.drawEllipse(QRectF(26, 26, 12, 12));
    } else if (nome == QLatin1String("cassa")) {
        p.drawRoundedRect(QRectF(8, 30, 48, 24), 3, 3);
        p.drawRoundedRect(QRectF(16, 10, 32, 14), 3, 3);
        p.drawLine(32, 24, 32, 30);
        p.drawLine(18, 42, 46, 42);
    } else if (nome == QLatin1String("aspetto")) {
        p.drawEllipse(QRectF(8, 8, 48, 48));
        pieno(QRectF(18, 20, 8, 8));
        pieno(QRectF(30, 14, 8, 8));
        pieno(QRectF(41, 22, 8, 8));
        pieno(QRectF(22, 34, 10, 10));
    } else {
        pieno(QRectF(24, 24, 16, 16));
    }
    return QIcon(px);
}

// ---------------------------------------------------------------------------
// Menu laterale
// ---------------------------------------------------------------------------

SchedeLaterali::SchedeLaterali(QWidget* parent)
    : QTabWidget(parent)
{
    setTabBar(new BarraLaterale(this));
    setTabPosition(QTabWidget::West);
    setDocumentMode(true);
}

void SchedeLaterali::impostaIcona(int indice, const QString& nome)
{
    tabBar()->setTabData(indice, nome);
}

BarraLaterale::BarraLaterale(QWidget* parent)
    : QTabBar(parent)
{
    setMouseTracking(true);
    setExpanding(false);
    setUsesScrollButtons(true);
    setDrawBase(false);
}

QSize BarraLaterale::tabSizeHint(int index) const
{
    QFont f = font();
    f.setWeight(QFont::DemiBold);
    const int testo = QFontMetrics(f).horizontalAdvance(tabText(index));
    return {std::max(160, 52 + testo + 12), std::max(38, QFontMetrics(f).height() + 18)};
}

void BarraLaterale::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const auto& c = coloriAttuali();
    p.fillRect(rect(), c.superficieAlta);
    p.setPen(c.bordo);
    p.drawLine(rect().topRight(), rect().bottomRight());
    for (int i = 0; i < count(); ++i) {
        const QRect r = tabRect(i).adjusted(6, 2, -6, -2);
        const bool scelta = i == currentIndex();
        const bool attiva = isTabEnabled(i);
        if (scelta) {
            p.setPen(Qt::NoPen);
            p.setBrush(c.primarioTenue);
            p.drawRoundedRect(r, 7, 7);
            p.setBrush(c.primario);
            p.drawRoundedRect(QRect(r.left(), r.top() + 7, 4, r.height() - 14), 2, 2);
        } else if (i == m_sopra && attiva) {
            p.setPen(Qt::NoPen);
            p.setBrush(c.primarioTenue.lighter(104));
            p.drawRoundedRect(r, 7, 7);
        }
        const QColor colore = !attiva ? c.bordo.darker(115) : scelta ? c.primario : c.testoSecondario;
        const QString icona = tabData(i).toString();
        const int lato = 22;
        if (!icona.isEmpty())
            p.drawPixmap(r.left() + 14, r.center().y() - lato / 2, iconaMenu(icona, colore, 2 * lato).pixmap(lato, lato));
        QFont f = font();
        f.setWeight(scelta ? QFont::DemiBold : QFont::Normal);
        p.setFont(f);
        p.setPen(!attiva ? c.bordo.darker(115) : scelta ? c.primario : c.testo);
        p.drawText(r.adjusted(48, 0, -6, 0), Qt::AlignVCenter | Qt::AlignLeft, tabText(i));
    }
}

void BarraLaterale::mouseMoveEvent(QMouseEvent* e)
{
    const int sopra = tabAt(e->position().toPoint());
    if (sopra != m_sopra) {
        m_sopra = sopra;
        update();
    }
    QTabBar::mouseMoveEvent(e);
}

void BarraLaterale::leaveEvent(QEvent* e)
{
    m_sopra = -1;
    update();
    QTabBar::leaveEvent(e);
}

} // namespace mdr
