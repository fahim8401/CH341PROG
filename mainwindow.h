/*
 * Copyright (C) 2023 Mikhail Medvedev <e-ink-reader@yandex.ru>
 *
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */
#ifndef MAINWINDOW_H
#define MAINWINDOW_H
#include <QMainWindow>
#include <QDebug>
#include <QMessageBox>
#include <QFile>
#include <QFileDialog>
#include <QTimer>
#include <QSettings>
#include <QPlainTextEdit>
#include <unistd.h>
#include "qhexedit.h"
#include "dialogsp.h"
#include "searchdialog.h"
#include "toast.h"
#include "chipinfo.h"
extern "C" {
   #include "ch341a.h"
}


namespace Ui {
class MainWindow;
}

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

    void appendLog(const QString &msg);

public slots:

    void receiveAddr(QString);
    void receiveAddr2(QString);

private slots:
    void on_pushButton_clicked();
    void on_pushButton_2_clicked();
    void on_comboBox_size_currentIndexChanged(int index);
    void on_comboBox_page_currentIndexChanged(int index);
    void on_comboBox_speed_currentIndexChanged(int index);
    void on_actionDetect_triggered();
    void on_actionSave_triggered();
    void on_actionErase_triggered();
    void on_actionUndo_triggered();
    void on_actionRedo_triggered();
    void on_actionOpen_triggered();
    void on_actionWrite_triggered();
    void on_actionRead_triggered();
    void on_actionExit_triggered();
    void on_comboBox_man_currentIndexChanged(int index);
    void on_comboBox_name_currentIndexChanged(const QString &arg1);
    void on_actionVerify_triggered();
    void on_pushButton_3_clicked();
    void on_actionSave_Part_triggered();
    void on_actionLoad_Part_triggered();
    void on_actionFind_Replace_triggered();
    void on_actionUnprotect_triggered();
    void onEraseTick();

private:
    Ui::MainWindow *ui;
    int statusCH341;
    QByteArray chipData;
    uint32_t currentChipSize, currentNumBlocks, currentBlockSize;
    uint8_t currentSpeed;
    QHexEdit *hexEdit;
    QString fileName;
    QByteArray block;
    uint32_t blockStartAddr, blockLen;

    // theme / preferences
    bool m_darkTheme;
    QSettings *m_settings;

    // console + notifications
    QPlainTextEdit *m_log;
    Toast *m_toast;
    ChipPinoutWidget *m_pinout;

    // chip info state
    int m_lastVerdict;
    bool m_haveDetect;

    // async operation state
    QTimer *m_eraseTimer;
    int m_eraseTicks;
    bool m_busy;
    bool m_autoWrite, m_autoVerify;

    QString bytePrint(unsigned char z);
    QString detectVerdictText(int status, unsigned char man, unsigned char type, unsigned char cap);
    QVector <QString> chType = {"SPI_FLASH","25_EEPROM","93_EEPROM","24_EEPROM","95_EEPROM"};
    struct chip_data {
        QString chipManuf;
        QString chipTypeTxt;
        QString chipName;
        uint8_t chipJedecIDMan;
        uint8_t chipJedecIDDev;
        uint8_t chipJedecIDCap;
        uint32_t chipSize;
        uint16_t blockSize;
        uint8_t chipTypeHex;
        uint8_t algorithmCode;
        int delay;
        uint8_t extend;
        uint8_t eeprom;
        uint8_t eepromPages;
        QString chipVCC;
    };
    chip_data chips[2000];
    int max_rec;
    QString sizeConvert(int a);
    QString hexiAddr(uint32_t a);
    uint32_t hexToInt(QString str);
    void ch341StatusFlashing();

    void setBusy(bool busy);
    void finishErase(bool ok);
    void runWriteVerifySteps();
    void applyTheme(bool dark);
    void toggleTheme();
    void toastMsg(const QString &msg, Toast::Kind kind);
    void updateDashBoard();
    void navTo(int index);
    void onHexAddressChanged(qint64 addr);
    void onHexDataChanged();

    void doBlankCheck();
    void doFill();
    void doReadStatus();
    void doClearStatus();
    void doReadSfdp();
    void doCompareFile();
    void doExportRegion();
    void doGoto();
    void doCopy();
    void doClearLog();
    void doSaveLog();

    void updateChipInfoPage();
    void applyPinStates(int verdict);
    void setSignal(QLabel *lbl, const QString &state, const char *color);
    QString jedecManufName(unsigned char man);
};

#endif // MAINWINDOW_H
