#include "chipinfo.h"

#include <QPainter>
#include <QFont>

ChipPinoutWidget::ChipPinoutWidget(QWidget *parent) :
    QWidget(parent),
    m_dark(false),
    m_package("SOIC-8 / SOP-8")
{
    for (int i = 0; i < 9; i++)
        m_state[i] = Unknown;
    setMinimumSize(360, 300);
}

void ChipPinoutWidget::setDarkTheme(bool dark) { m_dark = dark; update(); }
void ChipPinoutWidget::setPackageName(const QString &name) { m_package = name; update(); }

void ChipPinoutWidget::setAllStates(PinState s)
{
    for (int i = 1; i <= 8; i++)
        m_state[i] = s;
    update();
}

void ChipPinoutWidget::setPinState(int pinNumber, PinState s)
{
    if (pinNumber >= 1 && pinNumber <= 8)
    {
        m_state[pinNumber] = s;
        update();
    }
}

QString ChipPinoutWidget::pinLabel(int pinNumber)
{
    switch (pinNumber)
    {
    case 1: return "CS#";
    case 2: return "DO / IO1";
    case 3: return "WP# / IO2";
    case 4: return "GND";
    case 5: return "DI / IO0";
    case 6: return "CLK";
    case 7: return "HOLD# / IO3";
    case 8: return "VCC";
    }
    return "?";
}

QString ChipPinoutWidget::pinRole(int pinNumber)
{
    switch (pinNumber)
    {
    case 1: return "Chip select (active low, /CS)";
    case 2: return "Data out / MISO (IO1 in quad)";
    case 3: return "Write protect (IO2 in quad)";
    case 4: return "Ground";
    case 5: return "Data in / MOSI (IO0 in quad)";
    case 6: return "Serial clock";
    case 7: return "Hold / reset (IO3 in quad)";
    case 8: return "Power supply (3.3 V, some 1.8 V)";
    }
    return "";
}

QColor ChipPinoutWidget::colorForState(PinState s) const
{
    switch (s)
    {
    case Ok:   return QColor("#22a06b");
    case Warn: return QColor("#e0a13b");
    case Bad:  return QColor("#d64541");
    default:   return m_dark ? QColor("#4a5568") : QColor("#9aa4b5");
    }
}

void ChipPinoutWidget::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);

    const int w = width();
    const int h = height();

    const QColor bodyFill = m_dark ? QColor("#1b2231") : QColor("#eef1f7");
    const QColor bodyEdge = m_dark ? QColor("#2a3550") : QColor("#b9c2d0");
    const QColor textCol  = m_dark ? QColor("#e7ecf6") : QColor("#1f2933");
    const QColor muted    = m_dark ? QColor("#8b95a7") : QColor("#6b7688");

    QFont f = p.font();

    // title
    f.setBold(true);
    p.setFont(f);
    p.setPen(textCol);
    p.drawText(QRect(0, 6, w, 20), Qt::AlignHCenter, m_package);
    f.setBold(false);
    p.setFont(f);

    // chip body
    const int bodyW = qBound(90, int(w * 0.34), 160);
    const int bodyH = qBound(150, int(h * 0.66), 250);
    const QRect body((w - bodyW) / 2, (h - bodyH) / 2, bodyW, bodyH);
    p.setBrush(bodyFill);
    p.setPen(QPen(bodyEdge, 2));
    p.drawRoundedRect(body, 8, 8);

    // pin-1 dot
    p.setBrush(muted);
    p.setPen(Qt::NoPen);
    p.drawEllipse(QPoint(body.left() + 12, body.top() + 12), 4, 4);

    const int pinLen = qBound(28, int(w * 0.12), 50);
    const int pitch = bodyH / 4;

    // left pins 1..4
    for (int i = 0; i < 4; i++)
    {
        const int pn = i + 1;
        const int y = body.top() + pitch * i + pitch / 2;
        const QColor c = colorForState(m_state[pn]);
        p.setPen(QPen(c, 3));
        p.drawLine(body.left() - pinLen, y, body.left(), y);
        p.setBrush(c);
        p.setPen(Qt::NoPen);
        p.drawRoundedRect(body.left() - pinLen - 6, y - 4, 10, 8, 2, 2);
        p.setPen(textCol);
        p.drawText(QRect(0, y - 22, body.left() - pinLen - 10, 44),
                   Qt::AlignRight | Qt::AlignVCenter,
                   QString("%1  %2").arg(pinLabel(pn)).arg(pn));
    }

    // right pins 5..8 (bottom to top)
    for (int i = 0; i < 4; i++)
    {
        const int pn = 5 + i;
        const int y = body.bottom() - pitch * i - pitch / 2;
        const QColor c = colorForState(m_state[pn]);
        p.setPen(QPen(c, 3));
        p.drawLine(body.right(), y, body.right() + pinLen, y);
        p.setBrush(c);
        p.setPen(Qt::NoPen);
        p.drawRoundedRect(body.right() + pinLen - 4, y - 4, 10, 8, 2, 2);
        p.setPen(textCol);
        p.drawText(QRect(body.right() + pinLen + 10, y - 22,
                         w - (body.right() + pinLen + 10), 44),
                   Qt::AlignLeft | Qt::AlignVCenter,
                   QString("%1  %2").arg(pn).arg(pinLabel(pn)));
    }

    // legend
    p.setPen(muted);
    p.drawText(QRect(0, h - 36, w, 16), Qt::AlignHCenter,
               "1 CS#  ·  2 DO  ·  3 WP#  ·  4 GND  ·  5 DI  ·  6 CLK  ·  7 HOLD#  ·  8 VCC");
    p.drawText(QRect(0, h - 18, w, 16), Qt::AlignHCenter,
               "Pin colour shows inferred connection status");
}
