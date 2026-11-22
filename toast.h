#ifndef TOAST_H
#define TOAST_H

#include <QLabel>

class QPropertyAnimation;
class QGraphicsOpacityEffect;

/* Small transient notification that overlays its parent window's top-right
 * corner and fades out. */
class Toast : public QLabel
{
    Q_OBJECT
public:
    enum Kind { Info = 0, Success = 1, Error = 2 };

    explicit Toast(QWidget *parent = nullptr);
    void popup(const QString &text, Kind kind = Info, int msec = 2600);

private:
    QGraphicsOpacityEffect *m_effect;
    QPropertyAnimation *m_anim;
};

#endif // TOAST_H
