#ifndef CHIPINFO_H
#define CHIPINFO_H

#include <QWidget>

/* Draws a SPI NOR flash chip (SOIC-8 / SOP-8) with its pin names, and colours
 * each pin according to the inferred connection status. */
class ChipPinoutWidget : public QWidget
{
    Q_OBJECT
public:
    enum PinState { Unknown = 0, Ok = 1, Warn = 2, Bad = 3 };

    explicit ChipPinoutWidget(QWidget *parent = nullptr);

    void setDarkTheme(bool dark);
    void setPackageName(const QString &name);
    void setAllStates(PinState s);
    void setPinState(int pinNumber, PinState s);
    static QString pinLabel(int pinNumber);
    static QString pinRole(int pinNumber);

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    QColor colorForState(PinState s) const;
    bool m_dark;
    QString m_package;
    PinState m_state[9];
};

#endif // CHIPINFO_H
