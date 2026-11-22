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
#include "mainwindow.h"
#include "ui_mainwindow.h"
#include <QCoreApplication>
#include <QEventLoop>
#include <QApplication>
#include <QClipboard>
#include <QMimeData>
#include <QLineEdit>
#include <QLabel>
#include <QVBoxLayout>
#include <QMessageBox>
#include <QButtonGroup>
#include <QDragEnterEvent>
#include <QDateTime>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QStyle>
#include <QDir>
#include <QTextCursor>
#include <cstring>
#include <QtGui>
#include "qhexedit.h"
#include "dialogsp.h"
#include "dialogrp.h"
#include "toast.h"

/* ---- logging bridge: route qDebug and ch341a messages to the console ---- */
static MainWindow *g_win = nullptr;

static void qtMessageHandler(QtMsgType type, const QMessageLogContext &, const QString &msg)
{
    Q_UNUSED(type);
    if (g_win)
        g_win->appendLog(msg);
    fprintf(stderr, "%s\n", qPrintable(msg));
}

extern "C" void ch341LogBridge(const char *s)
{
    if (g_win)
        g_win->appendLog(QString::fromUtf8(s));
}

MainWindow::MainWindow(QWidget *parent) :
    QMainWindow(parent),
    ui(new Ui::MainWindow)
{
    int index2;
    max_rec = 0;
    m_darkTheme = false;
    m_eraseTicks = 0;
    m_busy = false;
    m_autoWrite = false;
    m_autoVerify = false;
    m_log = nullptr;
    m_toast = nullptr;
    m_pinout = nullptr;
    m_settings = nullptr;
    m_eraseTimer = nullptr;
    m_lastVerdict = -1;
    m_haveDetect = false;
    hexEdit = nullptr;
    currentChipSize = 0;
    currentNumBlocks = 0;
    currentBlockSize = 0;
    currentSpeed = 0;
    blockStartAddr = 0;
    blockLen = 0;

    ui->setupUi(this);

    m_settings = new QSettings();
    m_log = ui->logView;
    m_toast = new Toast(this);
    m_pinout = new ChipPinoutWidget(ui->framePinout);
    {
        QVBoxLayout *pinLayout = new QVBoxLayout(ui->framePinout);
        pinLayout->setContentsMargins(0, 0, 0, 0);
        pinLayout->addWidget(m_pinout);
    }
    m_eraseTimer = new QTimer(this);
    m_eraseTimer->setInterval(500);
    connect(m_eraseTimer, SIGNAL(timeout()), this, SLOT(onEraseTick()));

    // hex editor lives inside the "frame" placeholder on the Hex Editor page
    hexEdit = new QHexEdit(ui->frame);
    QVBoxLayout *frameLayout = new QVBoxLayout(ui->frame);
    frameLayout->setContentsMargins(0, 0, 0, 0);
    frameLayout->addWidget(hexEdit);
    connect(hexEdit, &QHexEdit::currentAddressChanged, this, &MainWindow::onHexAddressChanged);
    connect(hexEdit, &QHexEdit::dataChanged, this, &MainWindow::onHexDataChanged);

    // combo boxes
    ui->comboBox_name->addItems({""});
    ui->comboBox_man->addItems({""});
    ui->comboBox_vcc->addItems({ " ", "3.3 V", "1.8 V", "5.0 V"});
    //0 = 20kHz; 1 = 100kHz, 2 = 400kHz, 3 = 750kHz
    ui->comboBox_speed->addItem("750 khz", 3);
    ui->comboBox_speed->addItem("400 khz", 2);
    ui->comboBox_speed->addItem("100 khz", 1);
    ui->comboBox_speed->addItem("20 khz", 0);

    ui->comboBox_page->addItem(" ", 0);
    ui->comboBox_page->addItem("1", 1);
    ui->comboBox_page->addItem("2", 2);
    ui->comboBox_page->addItem("4", 4);
    ui->comboBox_page->addItem("8", 8);
    ui->comboBox_page->addItem("16", 16);
    ui->comboBox_page->addItem("32", 32);
    ui->comboBox_page->addItem("64", 64);
    ui->comboBox_page->addItem("128", 128);
    ui->comboBox_page->addItem("256", 256);
    ui->comboBox_page->addItem("512", 512);

    ui->comboBox_size->addItem(" ", 0);
    ui->comboBox_size->addItem("64 K", 64 * 1024);
    ui->comboBox_size->addItem("128 K", 128 * 1024);
    ui->comboBox_size->addItem("256 K", 256 * 1024);
    ui->comboBox_size->addItem("512 K", 512 * 1024);
    ui->comboBox_size->addItem("1 M", 1024 * 1024);
    ui->comboBox_size->addItem("2 M", 2048 * 1024);
    ui->comboBox_size->addItem("4 M", 4096 * 1024);
    ui->comboBox_size->addItem("8 M", 8192 * 1024);
    ui->comboBox_size->addItem("16 M", 16384 * 1024);
    ui->comboBox_size->addItem("32 M", 32768 * 1024);
    ui->comboBox_size->addItem("64 M", 65536 * 1024);

    ui->comboBox_fill->addItems({"00 (zeros)", "FF (blank)", "Custom"});

    // navigation, theme, tool buttons
    connect(ui->btnNavProgrammer, &QPushButton::clicked, this, [this]() { navTo(0); });
    connect(ui->btnNavChipInfo,   &QPushButton::clicked, this, [this]() { navTo(4); });
    connect(ui->btnRefreshInfo,   &QPushButton::clicked, this, &MainWindow::on_pushButton_2_clicked);
    connect(ui->btnNavHex,        &QPushButton::clicked, this, [this]() { navTo(1); });
    connect(ui->btnNavTools,      &QPushButton::clicked, this, [this]() { navTo(2); });
    connect(ui->btnNavConsole,    &QPushButton::clicked, this, [this]() { navTo(3); });
    connect(ui->btnTheme,         &QPushButton::clicked, this, &MainWindow::toggleTheme);

    connect(ui->btnGoto,          &QPushButton::clicked, this, &MainWindow::doGoto);
    connect(ui->btnCopy,          &QPushButton::clicked, this, &MainWindow::doCopy);
    connect(ui->btnExportRegion,  &QPushButton::clicked, this, &MainWindow::doExportRegion);
    connect(ui->btnFindReplace,   &QPushButton::clicked, this, &MainWindow::on_actionFind_Replace_triggered);
    connect(ui->lineEdit_goto,    &QLineEdit::returnPressed, this, &MainWindow::doGoto);

    connect(ui->btnBlankCheck,    &QPushButton::clicked, this, &MainWindow::doBlankCheck);
    connect(ui->btnReadStatus,    &QPushButton::clicked, this, &MainWindow::doReadStatus);
    connect(ui->btnClearStatus,   &QPushButton::clicked, this, &MainWindow::doClearStatus);
    connect(ui->btnReadSfdp,      &QPushButton::clicked, this, &MainWindow::doReadSfdp);
    connect(ui->btnCompareFile,   &QPushButton::clicked, this, &MainWindow::doCompareFile);
    connect(ui->btnFill,          &QPushButton::clicked, this, &MainWindow::doFill);
    connect(ui->btnClearLog,      &QPushButton::clicked, this, &MainWindow::doClearLog);
    connect(ui->btnSaveLog,       &QPushButton::clicked, this, &MainWindow::doSaveLog);

    connect(ui->actionBlankCheck, &QAction::triggered, this, &MainWindow::doBlankCheck);
    connect(ui->actionReadStatus, &QAction::triggered, this, &MainWindow::doReadStatus);
    connect(ui->actionReadSfdp,   &QAction::triggered, this, &MainWindow::doReadSfdp);
    connect(ui->actionCompare,    &QAction::triggered, this, &MainWindow::doCompareFile);
    connect(ui->actionFill,       &QAction::triggered, this, &MainWindow::doFill);
    connect(ui->actionExportRegion, &QAction::triggered, this, &MainWindow::doExportRegion);
    connect(ui->actionGoto,       &QAction::triggered, this, &MainWindow::doGoto);
    connect(ui->actionToggleTheme, &QAction::triggered, this, &MainWindow::toggleTheme);
    connect(ui->actionToggleConsole, &QAction::triggered, this, [this]() { navTo(3); });

    // console + ch341a logging
    g_win = this;
    qInstallMessageHandler(qtMessageHandler);
    ch341aSetLogCallback(ch341LogBridge);

    // connect and status check
    appendLog("Connecting to CH341A programmer...");
    {
        QElapsedTimer ct;
        ct.start();
        statusCH341 = ch341Configure(0x1a86,0x5512); // VID 1a86 PID 5512 for CH341A
        ch341StatusFlashing();
        if (statusCH341 == 0)
            appendLog(QString("CH341A connected in %1 ms").arg(ct.elapsed()));
        else
            appendLog("CH341A not found on USB");
        ch341Release();
    }
    chipData.resize(256);
    for (int i=0; i < 256; i++)
    {
        chipData[i] = char(0xff);
    }
    hexEdit->setData(chipData);

    //opening chip database file
    appendLog("Opening chip database");
    QByteArray dataChips;
    QFile datfile("CH341a.Dat");
    if (!datfile.open(QIODevice::ReadOnly))
    {
        const QStringList dbPaths = {
            QCoreApplication::applicationDirPath() + "/CH341a.Dat",
            QStringLiteral("/opt/CH341SPI/CH341a.Dat"),
            QStringLiteral("/usr/share/CH341SPI/CH341a.Dat")
        };
        for (const QString &p : dbPaths)
        {
            datfile.setFileName(p);
            if (datfile.open(QIODevice::ReadOnly))
                break;
        }
    }
    if (!datfile.isOpen())
    {
        QMessageBox::about(this, "Error", "Error loading chip database file!");
        applyTheme(m_settings->value("theme", "light").toString() == "dark");
        return;
    }
    dataChips = datfile.readAll();
    datfile.close();
    //parsing dat file
    char txtBuf[0x30];
    int i, j, recNo, dataPoz, dataSize, delay;
    uint32_t chipSize;
    uint16_t blockSize;
    unsigned char tmpBuf;
    dataPoz = 0;
    recNo = 0;
    QStringList verticalHeader;
    dataSize = dataChips.length();
    while (dataPoz < dataSize)
    {
        for (j=0; j<0x30; j++)
             {
                 txtBuf[j] = 0;
             }
        j = 0;
             while ((j < 0x10) && (dataChips[recNo * 0x44 + j] != ',')) // ASCII data reading
             {
                 txtBuf[j] = dataChips[recNo * 0x44 + j];
                 j++;
             }
             if (txtBuf[1] == 0x00) break;
             chips[recNo].chipTypeTxt = QByteArray::fromRawData(txtBuf, 0x30);
         for (i=0; i<0x30; i++)
             {
                 txtBuf[i] = 0;
             }
         j++;
         i = 0;
         while ((i < 0x20) && (dataChips[recNo * 0x44 + j] != ',')) // ASCII data reading
         {
             txtBuf[i] = dataChips[recNo * 0x44 + j];
             j++;
             i++;
         }
             chips[recNo].chipManuf = QByteArray::fromRawData(txtBuf, 0x30);
             for (i=0; i<0x30; i++)
                 {
                     txtBuf[i] = 0;
                 }
             j++;
             i = 0;
             while ((i < 0x30) && (dataChips[recNo * 0x44 + j] != '\0')) // ASCII data reading
             {
                 txtBuf[i] = dataChips[recNo * 0x44 + j];
                 j++;
                 i++;
             }
             chips[recNo].chipName = QByteArray::fromRawData(txtBuf, 0x30);
             chips[recNo].chipJedecIDMan = static_cast<uint8_t>(dataChips[recNo * 0x44 + 0x32]);
             chips[recNo].chipJedecIDDev = static_cast<uint8_t>(dataChips[recNo * 0x44 + 0x31]);
             chips[recNo].chipJedecIDCap = static_cast<uint8_t>(dataChips[recNo * 0x44 + 0x30]);
             tmpBuf = static_cast<unsigned char>(dataChips[recNo * 0x44 + 0x34]);
             chipSize = tmpBuf;
             tmpBuf = static_cast<unsigned char>(dataChips[recNo * 0x44 + 0x35]);
             chipSize = chipSize + tmpBuf * 256;
             tmpBuf = static_cast<unsigned char>(dataChips[recNo * 0x44 + 0x36]);
             chipSize = chipSize + tmpBuf * 256 * 256;
             tmpBuf = static_cast<unsigned char>(dataChips[recNo * 0x44 + 0x37]);
             chipSize = chipSize + tmpBuf * 256 * 256 * 256;
             chips[recNo].chipSize = chipSize;
             tmpBuf = static_cast<unsigned char>(dataChips[recNo * 0x44 + 0x38]);
             blockSize = tmpBuf;
             tmpBuf = static_cast<unsigned char>(dataChips[recNo * 0x44 + 0x39]);
             blockSize = blockSize + tmpBuf * 256;
             chips[recNo].blockSize = blockSize;
             tmpBuf = static_cast<unsigned char>(dataChips[recNo * 0x44 + 0x3a]);
             chips[recNo].chipTypeHex = tmpBuf;
             tmpBuf = static_cast<unsigned char>(dataChips[recNo * 0x44 + 0x3b]);
             chips[recNo].algorithmCode = tmpBuf;
             tmpBuf = static_cast<unsigned char>(dataChips[recNo * 0x44 + 0x3c]);
             delay = tmpBuf;
             tmpBuf = static_cast<unsigned char>(dataChips[recNo * 0x44 + 0x3d]);
             delay = delay + tmpBuf * 256;
             chips[recNo].delay = delay;
             tmpBuf = static_cast<unsigned char>(dataChips[recNo * 0x44 + 0x3e]);
             chips[recNo].extend = tmpBuf;
             tmpBuf = static_cast<unsigned char>(dataChips[recNo * 0x44 + 0x40]);
             chips[recNo].eeprom = tmpBuf;
             tmpBuf = static_cast<unsigned char>(dataChips[recNo * 0x44 + 0x42]);
             chips[recNo].eepromPages = tmpBuf;
             tmpBuf = static_cast<unsigned char>(dataChips[recNo * 0x44 + 0x43]);
             if (tmpBuf == 0x00) chips[recNo].chipVCC = "3.3 V";
             if (tmpBuf == 0x01) chips[recNo].chipVCC = "1.8 V";
             if (tmpBuf == 0x02) chips[recNo].chipVCC = "5.0 V";
             dataPoz = dataPoz + 0x44; //next record
             verticalHeader.append(QString::number(recNo));
             recNo++;
    }
    max_rec = recNo;
    ui->comboBox_man->blockSignals(true);
    for (i = 0; i<max_rec; i++)
    {
        index2 = ui->comboBox_man->findText(chips[i].chipManuf);
                    if ( index2 == -1 ) ui->comboBox_man->addItem(chips[i].chipManuf);
    }
    ui->comboBox_man->setCurrentIndex(0);
    ui->comboBox_man->blockSignals(false);
    appendLog(QString("Loaded %1 chips from database").arg(max_rec));

    // restore window geometry + theme
    QByteArray geo = m_settings->value("geometry").toByteArray();
    if (!geo.isEmpty())
        restoreGeometry(geo);
    applyTheme(m_settings->value("theme", "light").toString() == "dark");
    ui->lblProgModel->setText("CH341A");
    ui->lblProgUsb->setText("1a86:5512");
    ui->lblInfoJedec->setText("-");
    updateDashBoard();
    updateChipInfoPage();
    navTo(0);
}

MainWindow::~MainWindow()
{
    if (m_settings)
    {
        m_settings->setValue("geometry", saveGeometry());
        delete m_settings;
    }
    qInstallMessageHandler(nullptr);
    g_win = nullptr;
    delete ui;
}

QString MainWindow::bytePrint(unsigned char z)
{
    unsigned char s;
    s = z / 16;
    if (s > 0x9) s = s + 0x37;
    else s = s + 0x30;
    z = z % 16;
    if (z > 0x9) z = z + 0x37;
    else z = z + 0x30;
    return QString(s) + QString(z);
}

QString MainWindow::detectVerdictText(int status, unsigned char man, unsigned char type, unsigned char cap)
{
    switch (static_cast<enum jedec_status>(status))
    {
    case JEDEC_NO_CHIP:
        return "No chip response (JEDEC ID = FF FF FF).\n\n"
               "The programmer reads nothing back from the chip. Check that:\n"
               " - the chip is fully seated in the socket or the clip is on correctly,\n"
               " - pin 1 orientation is correct,\n"
               " - CS, CLK, MOSI, MISO, VCC and GND are all connected,\n"
               " - the chip is powered (3.3 V, or 1.8 V for low-voltage parts).";
    case JEDEC_BUS_LOW:
        return "Data line stuck LOW (JEDEC ID = 00 00 00).\n\n"
               "The data line never goes high, which usually means a wiring or "
               "power problem: reversed or shifted chip orientation, wrong pinout, "
               "a shorted data pin, or a chip that has no power. Reseat the chip "
               "and check the clip wiring.";
    case JEDEC_UNSTABLE:
        return QString("Marginal pin contact (ID %1 %2 %3 detected, but not on every read).\n\n"
               "The chip was identified, so detection continued, but the readings "
               "were not consistent. This points at a loose socket or clip, dirty or "
               "oxidised pins, a flapping USB connection, or too high an SPI clock. "
               "Reseat the chip, clean the contacts and lower the speed for reliable "
               "reading and writing.")
               .arg(bytePrint(man), bytePrint(type), bytePrint(cap));
    case JEDEC_IO_ERROR:
    default:
        return "USB communication error while reading the JEDEC ID.\n\n"
               "The programmer did not answer. Reconnect the CH341A device and "
               "make sure the udev rule is installed.";
    }
}

/* ================= theme / console / dashboard ================= */

void MainWindow::applyTheme(bool dark)
{
    m_darkTheme = dark;
    QFile f(dark ? ":/res/styles/dark.qss" : ":/res/styles/light.qss");
    if (f.open(QFile::ReadOnly | QFile::Text))
        qApp->setStyleSheet(QString::fromUtf8(f.readAll()));
    if (hexEdit)
    {
        if (dark)
        {
            hexEdit->setAddressAreaColor(QColor("#161c29"));
            hexEdit->setAddressFontColor(QColor("#8b95a7"));
            hexEdit->setAsciiAreaColor(QColor("#10151f"));
            hexEdit->setAsciiFontColor(QColor("#dde3ee"));
            hexEdit->setHexFontColor(QColor("#dde3ee"));
            hexEdit->setHighlightingColor(QColor("#3d5a8a"));
            hexEdit->setSelectionColor(QColor("#2f6df6"));
        }
        else
        {
            hexEdit->setAddressAreaColor(QColor("#eef2fb"));
            hexEdit->setAddressFontColor(QColor("#5b6472"));
            hexEdit->setAsciiAreaColor(QColor("#f8fafc"));
            hexEdit->setAsciiFontColor(QColor("#1f2933"));
            hexEdit->setHexFontColor(QColor("#1f2933"));
            hexEdit->setHighlightingColor(QColor("#fff3a8"));
            hexEdit->setSelectionColor(QColor("#2563eb"));
        }
        hexEdit->update();
    }
    if (ui->btnTheme)
        ui->btnTheme->setText(dark ? "Light theme" : "Dark theme");
    if (m_pinout)
        m_pinout->setDarkTheme(dark);
    if (m_settings)
        m_settings->setValue("theme", dark ? "dark" : "light");
}

void MainWindow::toggleTheme()
{
    applyTheme(!m_darkTheme);
    toastMsg(m_darkTheme ? "Dark theme" : "Light theme", Toast::Info);
}

void MainWindow::appendLog(const QString &msg)
{
    if (!m_log)
        return;
    QString ts = QDateTime::currentDateTime().toString("HH:mm:ss");
    m_log->appendPlainText("[" + ts + "] " + msg);
    if (ui->chkAutoscroll && ui->chkAutoscroll->isChecked())
    {
        QTextCursor c = m_log->textCursor();
        c.movePosition(QTextCursor::End);
        m_log->setTextCursor(c);
    }
}

void MainWindow::toastMsg(const QString &msg, Toast::Kind kind)
{
    if (m_toast)
        m_toast->popup(msg, kind);
    appendLog(msg);
}

void MainWindow::updateDashBoard()
{
    QString name = ui->comboBox_name->currentText().trimmed();
    QString man = ui->comboBox_man->currentText().trimmed();
    ui->statChipVal->setText(name.isEmpty() ? "-" : (man + " " + name).trimmed());
    uint32_t sz = ui->comboBox_size->currentData().toUInt();
    ui->statSizeVal->setText(sz ? sizeConvert(sz) : "-");
    QString vcc = ui->comboBox_vcc->currentText().trimmed();
    ui->statVccVal->setText(vcc.isEmpty() ? "-" : vcc);
}

void MainWindow::navTo(int index)
{
    ui->pages->setCurrentIndex(index);
    ui->btnNavProgrammer->setChecked(index == 0);
    ui->btnNavChipInfo->setChecked(index == 4);
    ui->btnNavHex->setChecked(index == 1);
    ui->btnNavTools->setChecked(index == 2);
    ui->btnNavConsole->setChecked(index == 3);
}

void MainWindow::onHexAddressChanged(qint64 addr)
{
    ui->lblHexStatus->setText("Offset: 0x" + hexiAddr(static_cast<uint32_t>(addr)) +
                              "    Size: " + QString::number(chipData.size()) + " bytes");
}

void MainWindow::onHexDataChanged()
{
    ui->lblHexStatus->setText("Offset: 0x" + hexiAddr(0) +
                              "    Size: " + QString::number(hexEdit->data().size()) + " bytes (modified)");
}

/* ================= busy / async operation plumbing ================= */

void MainWindow::setBusy(bool busy)
{
    m_busy = busy;
    const bool en = !busy;
    ui->actionRead->setEnabled(en);
    ui->actionWrite->setEnabled(en);
    ui->actionErase->setEnabled(en);
    ui->actionVerify->setEnabled(en);
    ui->actionDetect->setEnabled(en);
    ui->pushButton->setEnabled(en);
    ui->pushButton_2->setEnabled(en);
    ui->pushButton_3->setEnabled(en);
    ui->btnBlankCheck->setEnabled(en);
    ui->btnReadStatus->setEnabled(en);
    ui->btnClearStatus->setEnabled(en);
    ui->btnReadSfdp->setEnabled(en);
}

void MainWindow::finishErase(bool ok)
{
    ch341Release();
    ui->statusBar->showMessage("");
    ui->progressBar->setValue(0);
    ui->opStatus->setText("Idle");
    setBusy(false);
    if (ok)
    {
        ui->statusBar->showMessage("Erase complete", 5000);
        toastMsg("Erase complete", Toast::Success);
    }
    if (ok && (m_autoWrite || m_autoVerify))
    {
        runWriteVerifySteps();
    }
    else
    {
        m_autoWrite = false;
        m_autoVerify = false;
    }
}

void MainWindow::runWriteVerifySteps()
{
    setBusy(false);
    if (m_autoWrite)
    {
        m_autoWrite = false;
        on_actionWrite_triggered();
    }
    if (m_autoVerify)
    {
        m_autoVerify = false;
        on_actionVerify_triggered();
    }
}

void MainWindow::onEraseTick()
{
    int s = ch341ReadStatus();
    if (s < 0)
    {
        m_eraseTimer->stop();
        finishErase(false);
        QMessageBox::about(this, "Erase error", "USB error while waiting for the erase to finish.");
        return;
    }
    if (s == 0)
    {
        m_eraseTimer->stop();
        finishErase(true);
        return;
    }
    m_eraseTicks++;
    ui->progressBar->setValue(m_eraseTicks);
    ui->opStatus->setText(QString("Erasing... (%1 s)").arg(m_eraseTicks / 2));
    if (m_eraseTicks >= 120)
    {
        m_eraseTimer->stop();
        finishErase(false);
        QMessageBox::about(this, "Erase timeout",
            "Erase did not complete in time.\n\nThe chip may not be responding "
            "(check the socket/clip contact and VCC) or it needs more time.");
    }
}

void MainWindow::ch341StatusFlashing()
{
    if (statusCH341 == 0)
    {
        ui->eStatus->setText("Connected");
        ui->eStatus->setProperty("state", "ok");
        ui->statConnVal->setText("Connected");
    }
    else
    {
        ui->eStatus->setText("Not connected");
        ui->eStatus->setProperty("state", "bad");
        ui->statConnVal->setText("Not connected");
    }
    ui->eStatus->style()->unpolish(ui->eStatus);
    ui->eStatus->style()->polish(ui->eStatus);

    int maj = -1, min = -1;
    ch341GetRevision(&maj, &min);
    if (statusCH341 == 0 && maj >= 0)
        ui->lblProgVersion->setText(QString("%1.%2").arg(maj).arg(min, 2, 10, QChar('0')));
    else if (statusCH341 == 0)
        ui->lblProgVersion->setText("connected");
    else
        ui->lblProgVersion->setText("-");
    ui->lblProgConn->setText(statusCH341 == 0 ? "Connected" : "Not connected");
    ui->lblProgConn->setStyleSheet(QString("color:%1;font-weight:600;")
                                   .arg(statusCH341 == 0 ? "#22a06b" : "#d64541"));
}

void MainWindow::on_pushButton_clicked()
{
    if (m_busy) return;
    statusCH341 = ch341Configure(0x1a86,0x5512);
    ch341StatusFlashing();
    ch341SetStream(currentSpeed);
    setBusy(true);
    //Reading data from chip
    if ((currentNumBlocks > 0) && (currentBlockSize >0))
    {
    uint32_t addr = 0;
    uint32_t curBlock = 0;
    uint32_t j, k;
    int res;
    //progerssbar settings
    ui->progressBar->setRange(0, static_cast<int>(currentNumBlocks));
    ui->progressBar->setValue(0);
    uint8_t *buf;
    buf = (uint8_t *)malloc(currentBlockSize);
    ui->opStatus->setText("Reading " + ui->comboBox_name->currentText() + "...");
    ui->statusBar->showMessage("Reading data from " + ui->comboBox_name->currentText());
    appendLog("Read started: " + ui->comboBox_name->currentText());
    for (k = 0; k < currentNumBlocks; k++)
      {
         res = ch341SpiRead(buf, curBlock * currentBlockSize, currentBlockSize);
         if (statusCH341 != 0)
           {
             QMessageBox::about(this, "Error", "Programmer CH341a is not connected!");
             break;
           }
         if (res != 3)
           {
             QMessageBox::about(this, "Error", "Error reading sector " + QString::number(curBlock));
             break;
           }
         for (j = 0; j < currentBlockSize; j++)
            {
                  chipData[addr + j] = char(buf[addr + j - k * currentBlockSize]);
            }
         addr = addr + currentBlockSize;
         curBlock++;
         if (curBlock == 2) hexEdit->setData(chipData);
         ui->progressBar->setValue(static_cast<int>(curBlock));
         QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
      }
      if ((curBlock == currentNumBlocks) && (currentNumBlocks > 0))
      {
          bool allFF = true, all00 = true;
          for (j = 0; j < currentChipSize; j++)
          {
              unsigned char b = static_cast<unsigned char>(chipData[static_cast<int>(j)]);
              if (b != 0xFF) allFF = false;
              if (b != 0x00) all00 = false;
              if (!allFF && !all00) break;
          }
          if (allFF)
          {
              QMessageBox::about(this, "Read warning",
                  "The whole read is 0xFF.\n\nThe chip may be blank/erased, or it may "
                  "not be making proper contact (pin detection problem). Check the "
                  "socket or clip, pin 1 orientation and VCC/GND, then press 'Detect' "
                  "again.");
          }
          else if (all00)
          {
              QMessageBox::about(this, "Read warning",
                  "The whole read is 0x00.\n\nThis usually means a wiring problem: the "
                  "data line is stuck low, the chip is reversed or offset in the socket, "
                  "or it has no power. Check the connection and press 'Detect' again.");
          }
          else
          {
              toastMsg("Read complete (" + QString::number(currentChipSize/1024) + " KiB)", Toast::Success);
          }
          free(buf);
      }
    }
    else
    {
     QMessageBox::about(this, "Error", "Before reading from chip please press 'Detect' button.");
    }
    hexEdit->setData(chipData);
    ui->statusBar->showMessage("");
    ui->progressBar->setValue(0);
    ui->opStatus->setText("Idle");
    ch341Release();
    setBusy(false);
}

QString MainWindow::sizeConvert(int a)
{
    QString rez;
    rez = "0";
    if (a < 1024) rez = QString::number(a) + " B";
    else if ((a < 1024 * 1024)) rez = QString::number(a/1024) + " K";
    else rez = QString::number(a/1024/1024) + " M";
    return rez;
}

void MainWindow::on_pushButton_2_clicked()
{
    if (m_busy) return;
    //searching the connected chip in database
    statusCH341 = ch341Configure(0x1a86,0x5512);
    ch341StatusFlashing();
    ch341SetStream(currentSpeed);
    if (statusCH341 != 0)
      {
        QMessageBox::about(this, "Error", "Programmer CH341a is not connected!");
        return;
      }

    ui->opStatus->setText("Detecting chip...");
    appendLog("Detecting chip...");
    int i, index;
    bool found = false;
    // print JEDEC info
    struct JEDEC listJEDEC;
    enum jedec_status jedecStatus;
    listJEDEC = ch341DetectJedec(&jedecStatus);
    if ((jedecStatus == JEDEC_NO_CHIP) || (jedecStatus == JEDEC_BUS_LOW) || (jedecStatus == JEDEC_IO_ERROR))
    {
        QString msg = detectVerdictText(static_cast<int>(jedecStatus), listJEDEC.man, listJEDEC.type, listJEDEC.capacity);
        appendLog(msg);
        ui->statJedecVal->setText("-");
        ui->statChipVal->setText("-");
        ui->lblInfoJedec->setText("-");
        m_lastVerdict = static_cast<int>(jedecStatus);
        m_haveDetect = false;
        updateChipInfoPage();
        ui->opStatus->setText("Idle");
        toastMsg("Detection failed", Toast::Error);
        QMessageBox::about(this, "Detect error", msg);
        ch341Release();
        return;
    }
    if (jedecStatus == JEDEC_UNSTABLE)
    {
        toastMsg("Marginal pin contact - chip detected but not on every read, reseat it", Toast::Info);
    }
    ui->statJedecVal->setText(bytePrint(listJEDEC.man) + " " + bytePrint(listJEDEC.type) + " " + bytePrint(listJEDEC.capacity));
    for (i = 0; i< max_rec; i++)
    {
        if ((listJEDEC.man == chips[i].chipJedecIDMan) && (listJEDEC.type == chips[i].chipJedecIDDev) && (listJEDEC.capacity == chips[i].chipJedecIDCap))
        {
            index = ui->comboBox_man->findText(chips[i].chipManuf);
                        if ( index != -1 )
                        {
                           ui->comboBox_man->setCurrentIndex(index);
                        }
                        index = ui->comboBox_name->findText(chips[i].chipName);
                                    if ( index != -1 )
                                    {
                                       ui->comboBox_name->setCurrentIndex(index);
                                    }

            index = ui->comboBox_size->findData(chips[i].chipSize);
            if ( index != -1 )
            {
               ui->comboBox_size->setCurrentIndex(index);
            }
            index = ui->comboBox_page->findData(chips[i].blockSize);
            if ( index != -1 )
            {
               ui->comboBox_page->setCurrentIndex(index);
            }
            index = ui->comboBox_vcc->findText(chips[i].chipVCC);
            if ( index != -1 )
            {
               ui->comboBox_vcc->setCurrentIndex(index);
            }
            found = true;
            break;
        }
    }
    if (!found)
    {
        QMessageBox::about(this, "Detect",
            "Chip detected: JEDEC ID " + bytePrint(listJEDEC.man) + " " + bytePrint(listJEDEC.type) + " " + bytePrint(listJEDEC.capacity) +
            "\n\nThe chip is not in the database, so size and page size were not filled in. "
            "Select them manually before reading.");
        toastMsg("Chip detected but not in database", Toast::Info);
    }
    else
    {
        toastMsg("Detected: " + ui->comboBox_name->currentText(), Toast::Success);
    }
    m_lastVerdict = static_cast<int>(jedecStatus);
    m_haveDetect = (jedecStatus == JEDEC_OK);
    ui->lblInfoJedec->setText(bytePrint(listJEDEC.man) + " " + bytePrint(listJEDEC.type) + " " + bytePrint(listJEDEC.capacity));
    appendLog(QString("JEDEC ID %1 %2 %3 - %4")
              .arg(bytePrint(listJEDEC.man), bytePrint(listJEDEC.type), bytePrint(listJEDEC.capacity))
              .arg(jedecManufName(listJEDEC.man)));
    updateChipInfoPage();
    ui->opStatus->setText("Idle");
    updateDashBoard();
    ch341Release();
}

void MainWindow::on_comboBox_size_currentIndexChanged(int index)
{
    currentChipSize = ui->comboBox_size->currentData().toUInt();
    currentBlockSize = ui->comboBox_page->currentData().toUInt();
    if ((currentChipSize !=0) && (currentBlockSize!=0))
    {
        currentNumBlocks = currentChipSize / currentBlockSize;
        chipData.resize(static_cast<int>(currentChipSize));
        for (uint32_t i=0; i < currentChipSize; i++)
        {
            chipData[i] = char(0xff);
        }
        hexEdit->setData(chipData);
    }
    updateDashBoard();
    index = index + 0;
}

void MainWindow::on_comboBox_page_currentIndexChanged(int index)
{
    currentChipSize = ui->comboBox_size->currentData().toUInt();
    currentBlockSize = ui->comboBox_page->currentData().toUInt();
    if ((currentChipSize !=0) && (currentBlockSize!=0))
    {
        currentNumBlocks = currentChipSize / currentBlockSize;
        chipData.resize(static_cast<int>(currentChipSize));
        for (uint32_t i=0; i < currentChipSize; i++)
        {
            chipData[i] = char(0xff);
        }
        hexEdit->setData(chipData);
    }
    index = index + 0;
}

void MainWindow::on_comboBox_speed_currentIndexChanged(int index)
{
    if (ui->comboBox_speed->currentData().toInt() != -1)
    {
       currentSpeed =  static_cast<unsigned char>(ui->comboBox_speed->currentData().toInt());
       ch341SetStream(currentSpeed);
    }
    index = index + 0;
}

void MainWindow::on_actionDetect_triggered()
{
   MainWindow::on_pushButton_2_clicked();
}

void MainWindow::on_actionSave_triggered()
{
    appendLog("Save file");
    fileName = QFileDialog::getSaveFileName(this,
                                QString::fromUtf8("Save file"),
                                m_settings->value("lastDir", QDir::currentPath()).toString(),
                                "Data Images (*.bin);;All files (*.*)");
    if (fileName.isEmpty())
        return;
    QFile file(fileName);
    if (!file.open(QIODevice::WriteOnly))
    {
        QMessageBox::about(this, "Error", "Error saving file!");
        return;
    }
    file.write(hexEdit->data());
    file.close();
    m_settings->setValue("lastDir", QFileInfo(fileName).absolutePath());
    toastMsg("Saved " + QFileInfo(fileName).fileName(), Toast::Success);
}

void MainWindow::on_actionErase_triggered()
{
    if (m_busy) return;
    statusCH341 = ch341Configure(0x1a86,0x5512);
    ch341StatusFlashing();
    ch341SetStream(currentSpeed);
    if (statusCH341 != 0)
      {
        QMessageBox::about(this, "Error", "Programmer CH341a is not connected!");
        return;
      }
    if ((currentNumBlocks == 0) || (currentBlockSize == 0))
      {
        QMessageBox::about(this, "Error", "Before erasing please press 'Detect' button.");
        ch341Release();
        return;
      }
    ui->statusBar->showMessage("Erasing the " + ui->comboBox_name->currentText());
    appendLog("Erase started: " + ui->comboBox_name->currentText());
    ui->opStatus->setText("Erasing...");
    setBusy(true);
    ui->progressBar->setRange(0, 120);
    ui->progressBar->setValue(0);
    ch341EraseChip();
    m_eraseTicks = 0;
    m_eraseTimer->start();
}

void MainWindow::on_actionUndo_triggered()
{
    hexEdit->undo();
}

void MainWindow::on_actionRedo_triggered()
{
    hexEdit->redo();
}

void MainWindow::on_actionOpen_triggered()
{
    appendLog("Open file");
    fileName = QFileDialog::getOpenFileName(this,
                                QString::fromUtf8("Open file"),
                                m_settings->value("lastDir", QDir::currentPath()).toString(),
                                "Data Images (*.bin);;All files (*.*)");
    if (fileName.isEmpty())
        return;
    QFile file(fileName);
    if (!file.open(QIODevice::ReadOnly))
    {
        return;
    }
    chipData = file.readAll();
    hexEdit->setData(chipData);
    file.close();
    m_settings->setValue("lastDir", QFileInfo(fileName).absolutePath());
    appendLog(QString("Loaded %1 (%2 bytes)").arg(QFileInfo(fileName).fileName()).arg(chipData.size()));
    toastMsg("Opened " + QFileInfo(fileName).fileName(), Toast::Success);
}

void MainWindow::on_actionWrite_triggered()
{
    if (m_busy) return;
    //Writting data to chip
    statusCH341 = ch341Configure(0x1a86,0x5512);
    ch341StatusFlashing();
    ch341SetStream(currentSpeed);
    if (statusCH341 != 0)
      {
        QMessageBox::about(this, "Error", "Programmer CH341a is not connected!");
        return;
      }
    setBusy(true);
    if ((currentNumBlocks > 0) && (currentBlockSize >0))
    {
    uint32_t addr = 0;
    uint32_t curBlock = 0;
    uint32_t j, k;
    int res;
    ui->statusBar->showMessage("Writing data to " + ui->comboBox_name->currentText());
    ui->opStatus->setText("Writing " + ui->comboBox_name->currentText() + "...");
    appendLog("Write started: " + ui->comboBox_name->currentText());
    ui->progressBar->setRange(0, static_cast<int>(currentNumBlocks));
    chipData = hexEdit->data();
    uint8_t *buf;
    buf = (uint8_t *)malloc(currentBlockSize);
    for (k = 0; k < currentNumBlocks; k++)
      {

         for (j = 0; j < currentBlockSize; j++)
            {
               buf[addr + j - k * currentBlockSize] = static_cast<uint8_t>(chipData[addr + j]) ;
            }
         res = ch341SpiWrite(buf, curBlock * currentBlockSize, currentBlockSize);
         if (statusCH341 != 0)
           {
             QMessageBox::about(this, "Error", "Programmer CH341a is not connected!");
             break;
           }
         if (res != 0)
           {
             QMessageBox::about(this, "Error", "Error writing sector " + QString::number(curBlock));
             break;
           }
         addr = addr + currentBlockSize;
         curBlock++;
         ui->progressBar->setValue(static_cast<int>(curBlock));
         QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents);

      }
      if ((curBlock == currentNumBlocks) && (currentNumBlocks > 0))
          toastMsg("Write complete", Toast::Success);
      free(buf);
    }
    else
    {
     QMessageBox::about(this, "Error", "Before reading from chip please press 'Detect' button.");
    }
    ui->progressBar->setValue(0);
    ui->opStatus->setText("Idle");
    ui->statusBar->showMessage("");
    ch341Release();
    setBusy(false);
}

void MainWindow::on_actionRead_triggered()
{
    MainWindow::on_pushButton_clicked();
}

void MainWindow::on_actionExit_triggered()
{
    ch341Release();
    MainWindow::close();
}

void MainWindow::on_comboBox_man_currentIndexChanged(int index)
{
    int i, index2;
    QString txt="";
    if (max_rec > 0)
    {
       txt = ui->comboBox_man->currentText().toUtf8();
       ui->comboBox_name->clear();
       ui->comboBox_name->addItem("");
       for (i = 0; i<max_rec; i++)
       {
           if (txt.compare(chips[i].chipManuf)==0)
           {
           index2 = ui->comboBox_name->findText(chips[i].chipName);
                    if ( index2 == -1 ) ui->comboBox_name->addItem(chips[i].chipName);
           }
       }
       ui->comboBox_name->setCurrentIndex(0);
       ui->comboBox_vcc->setCurrentIndex(0);
       ui->comboBox_page->setCurrentIndex(0);
       ui->comboBox_size->setCurrentIndex(0);
       ui->statusBar->showMessage("");
       updateDashBoard();
    }
    index = index + 0;
}

void MainWindow::on_comboBox_name_currentIndexChanged(const QString &arg1)
{
    int i, index;
    QString manName = ui->comboBox_man->currentText();
    if (arg1.compare("") !=0)
    {

       for (i = 0; i < max_rec; i++)
       {
           if ((manName.compare(chips[i].chipManuf)==0) && (arg1.compare(chips[i].chipName)==0))
           {
               index = ui->comboBox_size->findData(chips[i].chipSize);
               if ( index != -1 )
               {
                  ui->comboBox_size->setCurrentIndex(index);
               }
               index = ui->comboBox_page->findData(chips[i].blockSize);
               if ( index != -1 )
               {
                  ui->comboBox_page->setCurrentIndex(index);
               }
               index = ui->comboBox_vcc->findText(chips[i].chipVCC);
               if ( index != -1 )
               {
                  ui->comboBox_vcc->setCurrentIndex(index);
               }
           }
       }
       updateDashBoard();

    }
}

void MainWindow::on_actionVerify_triggered()
{
    if (m_busy) return;
    //Reading data from chip
    statusCH341 = ch341Configure(0x1a86,0x5512);
    ch341StatusFlashing();
    ch341SetStream(currentSpeed);
    setBusy(true);
    if ((currentNumBlocks > 0) && (currentBlockSize >0))
    {
    uint32_t addr = 0;
    uint32_t curBlock = 0;
    uint32_t j, k;
    int  res;
    ui->progressBar->setRange(0, static_cast<int>(currentNumBlocks));
    ui->progressBar->setValue(0);
    uint8_t *buf;
    buf = (uint8_t *)malloc(currentBlockSize);
    chipData = hexEdit->data();
    ui->statusBar->showMessage("Verifying data from " + ui->comboBox_name->currentText());
    ui->opStatus->setText("Verifying " + ui->comboBox_name->currentText() + "...");
    appendLog("Verify started: " + ui->comboBox_name->currentText());
    for (k = 0; k < currentNumBlocks; k++)
      {
         res = ch341SpiRead(buf, curBlock * currentBlockSize, currentBlockSize);
         if (statusCH341 != 0)
           {
             QMessageBox::about(this, "Error", "Programmer CH341a is not connected!");
             break;
           }
         if (res != 3)
           {
             QMessageBox::about(this, "Error", "Error reading sector " + QString::number(curBlock));
             break;
           }
         for (j = 0; j < currentBlockSize; j++)
            {
                 if (chipData[addr + j] != char(buf[addr + j - k * currentBlockSize]))
                 {
                     QMessageBox::about(this, "Error", "Error comparing data!\nAddress:   " + hexiAddr(addr + j) + "\nBuffer: " + bytePrint(static_cast<unsigned char>(chipData[addr + j])) + "    Chip: " + bytePrint(static_cast<unsigned char>(buf[addr + j - k * currentBlockSize])));
                     ui->statusBar->showMessage("");
                     ui->opStatus->setText("Idle");
                     toastMsg("Verify FAILED at 0x" + hexiAddr(addr + j), Toast::Error);
                     free(buf);
                     ch341Release();
                     setBusy(false);
                     return;
                 }
            }
         addr = addr + currentBlockSize;
         curBlock++;
         ui->progressBar->setValue(static_cast<int>(curBlock));
         QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
      }
      if ((curBlock == currentNumBlocks) && (currentNumBlocks > 0))
          toastMsg("Verify OK", Toast::Success);
      free(buf);
    }
    else
    {
     QMessageBox::about(this, "Error", "Before reading from chip please press 'Detect' button.");
    }
    ui->statusBar->showMessage("");
    ui->progressBar->setValue(0);
    ui->opStatus->setText("Idle");
    ch341Release();
    setBusy(false);
}

QString MainWindow::hexiAddr(uint32_t add)
{
 QString rez = "";
 uint8_t A,B,C,D;
 D = 0xFF & add;
 add = add >> 8;
 C = 0xFF & add;
 add = add >> 8;
 B = 0xFF & add;
 add = add >> 8;
 A = 0xFF & add;
 rez = bytePrint(A) + bytePrint(B) + bytePrint(C) + bytePrint(D);
 return rez;
}

void MainWindow::on_pushButton_3_clicked()
{
    if (m_busy) return;
    m_autoWrite = ui->checkBox_2->isChecked();
    m_autoVerify = ui->checkBox_3->isChecked();
    if (ui->checkBox->isChecked())
    {
        // Erase is asynchronous; write/verify run when the erase finishes.
        on_actionErase_triggered();
    }
    else
    {
        runWriteVerifySteps();
    }
}

void MainWindow::receiveAddr(QString addressData)
{
    uint32_t ee, blockEndAddr = 0;
    int e,t;
    QString endType;
    e = addressData.indexOf("-");
    t = addressData.length();
    blockStartAddr = 0;
    blockLen = 0;
    endType = addressData.mid(t - 1, 1);
    blockStartAddr = hexToInt(addressData.mid(0, e));
    if (endType.compare("*")==0)
    {
        blockEndAddr = hexToInt(addressData.mid(e + 1, t - e - 2));
        if (blockEndAddr < blockStartAddr)
        {
            QMessageBox::about(this, "Error", "The end address must be greater than the starting addres.");
            return;
        }
        blockLen = blockEndAddr - blockStartAddr;
    }
    else blockLen = hexToInt(addressData.mid(e + 1, t - e - 2));
    block.resize(static_cast<int>(blockLen));
    chipData = hexEdit->data();
    for (ee = 0; ee < blockLen; ee++)
    {
        block[ee] = chipData[ee + blockStartAddr];
    }
    ui->statusBar->showMessage("Saving block");
    fileName = QFileDialog::getSaveFileName(this,
                                QString::fromUtf8("Save block"),
                                m_settings->value("lastDir", QDir::currentPath()).toString(),
                                "Data Images (*.bin);;All files (*.*)");
    if (fileName.isEmpty())
        return;
    QFile file(fileName);
    if (!file.open(QIODevice::WriteOnly))
    {
        QMessageBox::about(this, "Error", "Error saving file!");
        return;
    }
    file.write(block);
    file.close();
    m_settings->setValue("lastDir", QFileInfo(fileName).absolutePath());
    toastMsg("Region saved", Toast::Success);
}

void MainWindow::receiveAddr2(QString addressData)
{
    uint32_t ee;
    QString endType;
    blockStartAddr = 0;
    blockLen = 0;
    blockStartAddr = hexToInt(addressData);
    ui->statusBar->showMessage("Opening block");
    fileName = QFileDialog::getOpenFileName(this,
                                QString::fromUtf8("Open block"),
                                m_settings->value("lastDir", QDir::currentPath()).toString(),
                                "Data Images (*.bin);;All files (*.*)");
    if (fileName.isEmpty())
        return;
    ui->statusBar->showMessage("Current file: " + fileName);
    QFile file(fileName);
    if (!file.open(QIODevice::ReadOnly))
    {
        return;
    }
    block = file.readAll();
    blockLen = static_cast<uint32_t>(block.size());
    chipData = hexEdit->data();
    if (blockStartAddr + blockLen > static_cast<uint32_t>(chipData.size()))
    {
        QMessageBox::about(this, "Error", "The end address out of image size!");
        return;
    }
    for (ee=0; ee < blockLen; ee++)
    {
        chipData[ee + blockStartAddr] = block[ee];
    }
    hexEdit->setData(chipData);
    file.close();
    m_settings->setValue("lastDir", QFileInfo(fileName).absolutePath());
    ui->statusBar->showMessage("");
}

void MainWindow::on_actionSave_Part_triggered()
{
    DialogSP* savePartDialog = new DialogSP();
    savePartDialog->show();

    connect(savePartDialog, SIGNAL(sendAddr(QString)), this, SLOT(receiveAddr(QString)));
}

uint32_t MainWindow::hexToInt(QString str)
{
    unsigned char c;
    uint32_t len = static_cast<uint32_t>(str.length());
    QByteArray bstr = str.toLocal8Bit();
    if ((len > 0) && (len < 8))
    {
        uint32_t i, j = 1;
        uint32_t  addr = 0;
        for (i = len; i >0; i--)
        {
           c = static_cast<unsigned char>(bstr[i-1]);
           if ((c >= 0x30) && (c <=0x39)) addr =  addr + (c - 0x30) * j;
           if ((c >= 0x41) && (c <=0x46)) addr = addr + (c - 0x37) * j;
           if ((c >= 0x61) && (c <=0x66)) addr = addr + (c - 0x57) * j;
        j = j * 16;
        }
        return addr;
    }
    else return 0;
}

void MainWindow::on_actionLoad_Part_triggered()
{
    DialogRP* loadPartDialog = new DialogRP();
    loadPartDialog->show();

    connect(loadPartDialog, SIGNAL(sendAddr2(QString)), this, SLOT(receiveAddr2(QString)));
}

void MainWindow::on_actionFind_Replace_triggered()
{
    SearchDialog* searchDialog = new SearchDialog(hexEdit);
    searchDialog->show();
}

/* ================= advanced tools ================= */

void MainWindow::doBlankCheck()
{
    if (m_busy) return;
    statusCH341 = ch341Configure(0x1a86,0x5512);
    ch341StatusFlashing();
    ch341SetStream(currentSpeed);
    if (statusCH341 != 0)
    {
        QMessageBox::about(this, "Error", "Programmer CH341a is not connected!");
        return;
    }
    if (currentChipSize == 0)
    {
        QMessageBox::about(this, "Error", "Detect the chip first (or set size and page size).");
        ch341Release();
        return;
    }
    uint32_t start = hexToInt(ui->lineEdit_blankStart->text());
    uint32_t len = hexToInt(ui->lineEdit_blankLen->text());
    if (len == 0) len = currentChipSize - start;
    if ((start >= currentChipSize) || (start + len > currentChipSize) || (len == 0))
    {
        QMessageBox::about(this, "Error", "Invalid range for the detected chip size.");
        ch341Release();
        return;
    }
    setBusy(true);
    ui->progressBar->setRange(0, 1000);
    ui->progressBar->setValue(0);
    ui->opStatus->setText("Blank check...");
    appendLog(QString("Blank check 0x%1..0x%2").arg(start, 0, 16).arg(start + len, 0, 16));
    const uint32_t chunk = 4096;
    uint8_t *buf = (uint8_t *)malloc(chunk);
    uint32_t off = 0, firstBad = 0;
    bool blank = true;
    while (off < len)
    {
        uint32_t n = (len - off < chunk) ? (len - off) : chunk;
        if (ch341SpiRead(buf, start + off, n) < 0) break;
        for (uint32_t b = 0; b < n; b++)
            if (buf[b] != 0xFF) { blank = false; firstBad = start + off + b; break; }
        off += n;
        ui->progressBar->setValue(static_cast<int>(1000ULL * off / len));
        QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
        if (!blank) break;
    }
    free(buf);
    ui->progressBar->setValue(0);
    ui->opStatus->setText("Idle");
    ch341Release();
    setBusy(false);
    if (blank)
    {
        ui->lblBlankResult->setText("Blank: all 0xFF in the checked range.");
        toastMsg("Blank check: chip is blank", Toast::Success);
    }
    else
    {
        ui->lblBlankResult->setText("Not blank: first non-0xFF byte at 0x" + hexiAddr(firstBad));
        toastMsg("Blank check: data present", Toast::Error);
    }
}

void MainWindow::doFill()
{
    if (chipData.isEmpty())
    {
        toastMsg("Buffer is empty - read or open a file first", Toast::Error);
        return;
    }
    unsigned int val = 0xFF;
    int idx = ui->comboBox_fill->currentIndex();
    if (idx == 0) val = 0x00;
    else if (idx == 1) val = 0xFF;
    else
    {
        bool ok = false;
        val = ui->lineEdit_fillVal->text().toUInt(&ok, 16);
        if (!ok) val = 0xFF;
    }
    val &= 0xFF;
    chipData.fill(static_cast<char>(val));
    hexEdit->setData(chipData);
    toastMsg("Buffer filled with 0x" + QString("%1").arg(val, 2, 16, QChar('0')).toUpper(), Toast::Success);
}

void MainWindow::doReadStatus()
{
    if (m_busy) return;
    statusCH341 = ch341Configure(0x1a86,0x5512);
    ch341StatusFlashing();
    if (statusCH341 != 0)
    {
        QMessageBox::about(this, "Error", "Programmer CH341a is not connected!");
        return;
    }
    int sr = ch341ReadStatus();
    ch341Release();
    if (sr < 0)
    {
        ui->lblSrValue->setText("Read failed (USB error).");
        toastMsg("Status read failed", Toast::Error);
        return;
    }
    int wip = sr & 1, wel = (sr >> 1) & 1, bp0 = (sr >> 2) & 1,
        bp1 = (sr >> 3) & 1, bp2 = (sr >> 4) & 1, srp0 = (sr >> 7) & 1;
    QString t = QString("SR1 = 0x%1\nWIP=%2  WEL=%3\nBP2=%4 BP1=%5 BP0=%6  SRP0=%7\nProtection: %8")
        .arg(sr, 2, 16, QChar('0')).arg(wip).arg(wel)
        .arg(bp2).arg(bp1).arg(bp0).arg(srp0)
        .arg((bp0 || bp1 || bp2) ? "blocks protected" : "none");
    ui->lblSrValue->setText(t);
    toastMsg("Status register read", Toast::Info);
}

void MainWindow::doClearStatus()
{
    if (m_busy) return;
    statusCH341 = ch341Configure(0x1a86,0x5512);
    ch341StatusFlashing();
    if (statusCH341 != 0)
    {
        QMessageBox::about(this, "Error", "Programmer CH341a is not connected!");
        return;
    }
    ch341WriteStatus(0x00);
    int sr = ch341ReadStatus();
    ch341Release();
    ui->lblSrValue->setText(QString("Cleared. SR1 = 0x%1").arg(sr < 0 ? 0 : sr, 2, 16, QChar('0')));
    toastMsg("Status cleared / unprotected", Toast::Success);
}

void MainWindow::on_actionUnprotect_triggered()
{
    navTo(2);
    doClearStatus();
}

void MainWindow::doReadSfdp()
{
    if (m_busy) return;
    navTo(2);
    statusCH341 = ch341Configure(0x1a86,0x5512);
    ch341StatusFlashing();
    ch341SetStream(currentSpeed);
    if (statusCH341 != 0)
    {
        QMessageBox::about(this, "Error", "Programmer CH341a is not connected!");
        return;
    }
    uint8_t buf[256];
    memset(buf, 0, sizeof(buf));
    int r = ch341ReadSfdp(buf, sizeof(buf));
    ch341Release();
    if (r < 0)
    {
        ui->textEdit_sfdp->setPlainText("SFDP read failed (USB error).");
        toastMsg("SFDP read failed", Toast::Error);
        return;
    }
    QString out;
    bool ok = (buf[0] == 'S' && buf[1] == 'F' && buf[2] == 'D' && buf[3] == 'P');
    if (!ok)
    {
        out += "No SFDP signature found.\nThis chip may not support SFDP (older SPI NOR parts).\n\n";
    }
    else
    {
        int nph = buf[6];
        out += QString("SFDP signature OK. Revision %1.%2, %3 parameter header(s).\n\n")
                  .arg(buf[5]).arg(buf[4]).arg(nph);
        for (int h = 0; h < nph && h < 8; h++)
        {
            int off = 8 + h * 8;
            if (off + 8 > 256) break;
            uint16_t tid = buf[off] | (buf[off+1] << 8);
            uint32_t ptr = buf[off+1] | (buf[off+2] << 8) | (buf[off+3] << 16);
            uint8_t mjr = buf[off+4], mnr = buf[off+5];
            uint8_t tblLen = buf[off+7];
            out += QString("Header %1: id=0x%2 rev=%3.%4 len=%5 dwords ptr=0x%6\n")
                      .arg(h).arg(tid, 4, 16, QChar('0')).arg(mjr).arg(mnr).arg(tblLen)
                      .arg(ptr, 0, 16);
            if (tid == 0x0000 && ptr + 12 <= 256)
            {
                uint32_t density = (buf[ptr+8] | (buf[ptr+9] << 8) |
                                    (buf[ptr+10] << 16) | (buf[ptr+11] << 24)) & 0x7FFFFFFFu;
                if (density >= 3 && density < 40)
                {
                    unsigned long long bytes = 1ULL << (density - 3);
                    out += QString("  -> JEDEC BFPT: density %1 bits = %2 bytes (%3)\n")
                              .arg(density).arg(bytes).arg(sizeConvert(static_cast<int>(bytes)));
                    ui->statSizeVal->setText(sizeConvert(static_cast<int>(bytes)));
                }
            }
        }
        out += "\n";
    }
    out += "Raw SFDP (first 128 bytes):\n";
    for (int row = 0; row < 8; row++)
    {
        out += QString("%1: ").arg(row * 16, 4, 16, QChar('0'));
        for (int c = 0; c < 16; c++)
            out += QString("%1 ").arg(buf[row * 16 + c], 2, 16, QChar('0'));
        out += "\n";
    }
    ui->textEdit_sfdp->setPlainText(out);
    toastMsg(ok ? "SFDP read complete" : "No SFDP on this chip", ok ? Toast::Success : Toast::Info);
}

void MainWindow::doCompareFile()
{
    if (chipData.isEmpty())
    {
        toastMsg("Buffer is empty - read or open a file first", Toast::Error);
        return;
    }
    QString fn = QFileDialog::getOpenFileName(this, "Compare with file",
                        m_settings->value("lastDir", QDir::currentPath()).toString(),
                        "Data Images (*.bin);;All files (*.*)");
    if (fn.isEmpty())
        return;
    QFile f(fn);
    if (!f.open(QIODevice::ReadOnly))
    {
        toastMsg("Cannot open file", Toast::Error);
        return;
    }
    QByteArray other = f.readAll();
    f.close();
    int n = qMin(chipData.size(), other.size());
    int firstDiff = -1, diffs = 0;
    for (int i = 0; i < n; i++)
    {
        if (chipData[i] != other[i])
        {
            if (firstDiff < 0) firstDiff = i;
            diffs++;
        }
    }
    QString res;
    if (diffs == 0 && chipData.size() == other.size())
        res = "Identical (" + QString::number(n) + " bytes).";
    else
        res = QString("Differences: %1 byte(s). First at 0x%2.\nSizes: %3 / %4 bytes.")
                  .arg(diffs).arg(firstDiff < 0 ? 0 : firstDiff)
                  .arg(chipData.size()).arg(other.size());
    ui->lblCompareResult->setText(res);
    m_settings->setValue("lastDir", QFileInfo(fn).absolutePath());
    toastMsg(diffs == 0 ? "Files identical" : "Files differ", diffs == 0 ? Toast::Success : Toast::Error);
}

void MainWindow::doExportRegion()
{
    navTo(1);
    on_actionSave_Part_triggered();
}

void MainWindow::doGoto()
{
    navTo(1);
    bool ok = false;
    uint32_t a = ui->lineEdit_goto->text().toUInt(&ok, 16);
    if (!ok)
    {
        toastMsg("Enter a hex address", Toast::Error);
        return;
    }
    if (static_cast<qint64>(a) * 2 > hexEdit->data().size() * 2)
    {
        toastMsg("Address out of range", Toast::Error);
        return;
    }
    hexEdit->setCursorPosition(static_cast<qint64>(a) * 2);
    hexEdit->ensureVisible();
}

void MainWindow::doCopy()
{
    if (chipData.isEmpty())
    {
        toastMsg("Buffer is empty", Toast::Error);
        return;
    }
    QMimeData *md = new QMimeData();
    md->setData("application/octet-stream", chipData);
    QApplication::clipboard()->setMimeData(md);
    toastMsg("Buffer copied to clipboard (binary, " + QString::number(chipData.size()) + " bytes)", Toast::Success);
}

void MainWindow::doClearLog()
{
    if (m_log)
        m_log->clear();
}

void MainWindow::doSaveLog()
{
    QString fn = QFileDialog::getSaveFileName(this, "Save log",
                        m_settings->value("lastDir", QDir::currentPath()).toString(),
                        "Log files (*.log *.txt);;All files (*.*)");
    if (fn.isEmpty())
        return;
    QFile f(fn);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text))
    {
        toastMsg("Cannot write log", Toast::Error);
        return;
    }
    f.write(m_log->toPlainText().toUtf8());
    f.close();
    toastMsg("Log saved", Toast::Success);
}

/* ================= chip info page ================= */

QString MainWindow::jedecManufName(unsigned char man)
{
    switch (man)
    {
    case 0x01: return "Spansion / Cypress";
    case 0x1C: return "eON";
    case 0x1F: return "Atmel / Microchip";
    case 0x20: return "STMicroelectronics / Micron";
    case 0x37: return "AMIC";
    case 0x5E: return "Zbit";
    case 0x62: return "SANYO";
    case 0x7F: return "ISSI";
    case 0x89: return "Intel";
    case 0x8C: return "ESMT";
    case 0x9D: return "ISSI";
    case 0xA1: return "Fudan";
    case 0xAD: return "Hynix";
    case 0xBF: return "SST / Microchip";
    case 0xC2: return "Macronix";
    case 0xC8: return "GigaDevice";
    case 0xE0: return "Paragon";
    case 0xEF: return "Winbond";
    }
    return QString("Unknown (0x%1)").arg(man, 2, 16, QChar('0')).toUpper();
}

void MainWindow::setSignal(QLabel *lbl, const QString &state, const char *color)
{
    if (!lbl) return;
    lbl->setText(state);
    lbl->setStyleSheet(QString("color:%1;font-weight:600;").arg(color));
}

void MainWindow::applyPinStates(int verdict)
{
    if (!m_pinout) return;
    const ChipPinoutWidget::PinState OK   = ChipPinoutWidget::Ok;
    const ChipPinoutWidget::PinState WARN = ChipPinoutWidget::Warn;
    const ChipPinoutWidget::PinState BAD  = ChipPinoutWidget::Bad;
    const ChipPinoutWidget::PinState UNK  = ChipPinoutWidget::Unknown;

    QString detText;
    const char *detColor = "#6b7688";

    if (verdict == JEDEC_OK)
    {
        detText = "Chip connected - JEDEC ID read consistently. All SPI signals are responding.";
        detColor = "#22a06b";
        m_pinout->setAllStates(OK);
        setSignal(ui->lblSigCs,   "OK (chip selected)", "#22a06b");
        setSignal(ui->lblSigClk,  "OK (clocking)",      "#22a06b");
        setSignal(ui->lblSigMosi, "OK (commands seen)", "#22a06b");
        setSignal(ui->lblSigMiso, "OK (data read)",     "#22a06b");
        setSignal(ui->lblSigVcc,  "OK (chip powered)",  "#22a06b");
        setSignal(ui->lblSigGnd,  "OK",                 "#22a06b");
    }
    else if (verdict == JEDEC_UNSTABLE)
    {
        detText = "Chip detected but readings were unstable - poor pin contact. Reseat the chip / lower the bus speed.";
        detColor = "#e0a13b";
        m_pinout->setAllStates(WARN);
        setSignal(ui->lblSigCs,   "Marginal", "#e0a13b");
        setSignal(ui->lblSigClk,  "Marginal", "#e0a13b");
        setSignal(ui->lblSigMosi, "Marginal", "#e0a13b");
        setSignal(ui->lblSigMiso, "Marginal", "#e0a13b");
        setSignal(ui->lblSigVcc,  "Check",    "#e0a13b");
        setSignal(ui->lblSigGnd,  "Check",    "#e0a13b");
    }
    else if (verdict == JEDEC_NO_CHIP)
    {
        detText = "No chip response (JEDEC = FF FF FF). The chip is not seated, or a signal "
                  "(CS / CLK / MOSI / MISO / VCC / GND) is not connected.";
        detColor = "#d64541";
        m_pinout->setAllStates(BAD);
        setSignal(ui->lblSigCs,   "No response", "#d64541");
        setSignal(ui->lblSigClk,  "No response", "#d64541");
        setSignal(ui->lblSigMosi, "No response", "#d64541");
        setSignal(ui->lblSigMiso, "No data (FF)", "#d64541");
        setSignal(ui->lblSigVcc,  "Check power",  "#d64541");
        setSignal(ui->lblSigGnd,  "Check",        "#d64541");
    }
    else if (verdict == JEDEC_BUS_LOW)
    {
        detText = "Data line stuck low (JEDEC = 00 00 00). Reversed / unpowered chip or a short on the data line.";
        detColor = "#d64541";
        m_pinout->setAllStates(WARN);
        m_pinout->setPinState(2, BAD); // DO / MISO
        m_pinout->setPinState(4, BAD); // GND
        m_pinout->setPinState(8, BAD); // VCC
        setSignal(ui->lblSigCs,   "Check",          "#e0a13b");
        setSignal(ui->lblSigClk,  "Check",          "#e0a13b");
        setSignal(ui->lblSigMosi, "Check",          "#e0a13b");
        setSignal(ui->lblSigMiso, "Stuck low (00)", "#d64541");
        setSignal(ui->lblSigVcc,  "Check power",    "#d64541");
        setSignal(ui->lblSigGnd,  "Check",          "#d64541");
    }
    else if (verdict == JEDEC_IO_ERROR)
    {
        detText = "USB error talking to the programmer. Reconnect the CH341A device.";
        detColor = "#d64541";
        m_pinout->setAllStates(UNK);
        setSignal(ui->lblSigCs,   "-", "#6b7688");
        setSignal(ui->lblSigClk,  "-", "#6b7688");
        setSignal(ui->lblSigMosi, "-", "#6b7688");
        setSignal(ui->lblSigMiso, "-", "#6b7688");
        setSignal(ui->lblSigVcc,  "-", "#6b7688");
        setSignal(ui->lblSigGnd,  "-", "#6b7688");
    }
    else
    {
        detText = "Not detected yet - press Detect (or 'Re-detect chip') to read the connected chip.";
        detColor = "#6b7688";
        m_pinout->setAllStates(UNK);
        setSignal(ui->lblSigCs,   "-", "#6b7688");
        setSignal(ui->lblSigClk,  "-", "#6b7688");
        setSignal(ui->lblSigMosi, "-", "#6b7688");
        setSignal(ui->lblSigMiso, "-", "#6b7688");
        setSignal(ui->lblSigVcc,  "-", "#6b7688");
        setSignal(ui->lblSigGnd,  "-", "#6b7688");
    }

    ui->lblDetectStatus->setText(detText);
    ui->lblDetectStatus->setStyleSheet(QString("color:%1;font-weight:700;").arg(detColor));
}

void MainWindow::updateChipInfoPage()
{
    // chip details from the current selection
    QString name = ui->comboBox_name->currentText().trimmed();
    QString man = ui->comboBox_man->currentText().trimmed();
    ui->lblInfoName->setText(name.isEmpty() ? "-" : name);
    ui->lblInfoManuf->setText(man.isEmpty() ? "-" : man);
    uint32_t sz = ui->comboBox_size->currentData().toUInt();
    ui->lblInfoSize->setText(sz ? QString("%1  (%2 bytes)").arg(sizeConvert(static_cast<int>(sz))).arg(sz) : "-");
    uint32_t pg = ui->comboBox_page->currentData().toUInt();
    ui->lblInfoPage->setText(pg ? QString("%1 bytes").arg(pg) : "-");
    QString vcc = ui->comboBox_vcc->currentText().trimmed();
    ui->lblInfoVcc->setText(vcc.isEmpty() ? "-" : vcc);

    // extra fields from the matching database record
    QString type = "-", alg = "-", dly = "-", eep = "-";
    for (int i = 0; i < max_rec; i++)
    {
        if (man.compare(chips[i].chipManuf) == 0 && name.compare(chips[i].chipName) == 0)
        {
            type = chips[i].chipTypeTxt.trimmed();
            alg  = QString("0x%1").arg(chips[i].algorithmCode, 2, 16, QChar('0')).toUpper();
            dly  = QString("%1").arg(chips[i].delay);
            eep  = QString::number(chips[i].eepromPages);
            break;
        }
    }
    ui->lblInfoType->setText(type.isEmpty() ? "-" : type);
    ui->lblInfoAlg->setText(alg);
    ui->lblInfoDelay->setText(dly);
    ui->lblInfoEeprom->setText(eep);

    applyPinStates(m_lastVerdict);
}
