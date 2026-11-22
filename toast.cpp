#include "toast.h"

#include <QPropertyAnimation>
#include <QGraphicsOpacityEffect>
#include <QTimer>
#include <QStyle>

Toast::Toast(QWidget *parent) :
    QLabel(parent)
{
    setObjectName("toast");
    setAlignment(Qt::AlignCenter);
    setAttribute(Qt::WA_TransparentForMouseEvents, true);
    setWordWrap(false);
    setVisible(false);

    m_effect = new QGraphicsOpacityEffect(this);
    m_effect->setOpacity(1.0);
    setGraphicsEffect(m_effect);

    m_anim = new QPropertyAnimation(m_effect, "opacity", this);
    m_anim->setDuration(400);
    m_anim->setStartValue(1.0);
    m_anim->setEndValue(0.0);
    connect(m_anim, &QPropertyAnimation::finished, this, [this]() { setVisible(false); });
}

void Toast::popup(const QString &text, Kind kind, int msec)
{
    setText(text);
    setProperty("kind", kind == Error ? "error" : (kind == Success ? "success" : "info"));
    style()->unpolish(this);
    style()->polish(this);

    adjustSize();

    QWidget *p = parentWidget();
    if (p)
        move(p->width() - width() - 24, 20);

    m_anim->stop();
    m_effect->setOpacity(1.0);
    raise();
    setVisible(true);

    QTimer::singleShot(msec, this, [this]() { m_anim->start(); });
}
