/****************************************************************************
 *                                                                          *
 *   P2000T Cartridge Studio                                                *
 *   Copyright (C) 2023 Ivo Filot <ivo@ivofilot.nl>                         *
 *                                                                          *
 *   This program is free software: you can redistribute it and/or modify   *
 *   it under the terms of the GNU General Public License as                *
 *   published by the Free Software Foundation, either version 3 of the     *
 *   License, or (at your option) any later version.                        *
 *                                                                          *
 *   This program is distributed in the hope that it will be useful,        *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of         *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the          *
 *   GNU General Public License for more details.                           *
 *                                                                          *
 *   You should have received a copy of the GNU General Public license      *
 *   along with this program.  If not, see <https://www.gnu.org/licenses/>. *
 *                                                                          *
 ****************************************************************************/

#include "mainwindow.h"
#include "picoflasherapplication.h"
#include "config.h"
#include "logbuffer.h"

#include <QApplication>
#include <QDebug>
#include <QIcon>
#include <QStyle>
#include <QStyleFactory>
#include <QDateTime>
#include <QString>

std::shared_ptr<LogBuffer> log_messages;

/**
 * @brief Custom message handler for storing and displaying log messages.
 * @param type Qt message type
 * @param context message context
 * @param msg message text
 */
void message_output(QtMsgType type, const QMessageLogContext &context, const QString &msg) {
    Q_UNUSED(context);
    QDateTime date = QDateTime::currentDateTime();
    QString fmtime = date.toString("dd.MM.yyyy hh:mm:ss.zzz");

    switch (type) {
    case QtDebugMsg:
        log_messages->append(fmtime + " [DEBUG] " + msg);
        std::cout << "[DEBUG] " << msg.toStdString() << std::endl;
        break;
    case QtInfoMsg:
        log_messages->append(fmtime + " [INFO] " + msg);
        std::cout << "[INFO] " << msg.toStdString() << std::endl;
        break;
    case QtWarningMsg:
        log_messages->append(fmtime + " [WARNING] " + msg);
        std::cout << "[WARNING] " << msg.toStdString() << std::endl;
        break;
    case QtCriticalMsg:
        log_messages->append(fmtime + " [CRITICAL] " + msg);
        std::cerr << "[CRITICAL] " << msg.toStdString() << std::endl;
        break;
    case QtFatalMsg:
        log_messages->append(fmtime + " [FATAL] " + msg);
        std::cerr << "[FATAL] " << msg.toStdString() << std::endl;
        break;
    }
}

/**
 * @brief Main application entry point.
 * @param argc number of command line arguments
 * @param argv command line arguments
 * @return application exit code
 */
int main(int argc, char *argv[])
{
    QCoreApplication::setOrganizationName("Retrohacks.nl");
    QCoreApplication::setOrganizationDomain("retrohacks.nl");
    QCoreApplication::setApplicationName(PROGRAM_NAME);
    QCoreApplication::setApplicationVersion(PROGRAM_VERSION);

    CartridgeStudioApplication app(argc, argv);
    app.setWindowIcon(QIcon(PROGRAM_ICON));
#ifdef Q_OS_WIN
    if(QStyle* vista_style = QStyleFactory::create(QStringLiteral("windowsvista"))) {
        app.setStyle(vista_style);
    }
#endif
    qInfo() << "Qt widget style:" << app.style()->objectName();
    qRegisterMetaType<std::vector<uint8_t>>("stdvector_uint8_t");

    std::unique_ptr<MainWindow> mainWindow;
    log_messages = std::make_shared<LogBuffer>();

    try {
        // build main window
        qInstallMessageHandler(message_output);
        mainWindow = std::make_unique<MainWindow>(log_messages);
        mainWindow->setWindowTitle(QString(PROGRAM_NAME) + " " + QString(PROGRAM_VERSION));
    } catch(const std::exception& e) {
        std::cerr << "Error detected!" << std::endl;
        std::cerr << e.what() << std::endl;
        std::cerr << "Abnormal closing of program." << std::endl;
    }

    if(!mainWindow) {
        return EXIT_FAILURE;
    }
    mainWindow->show();

    int res = -1;
    try {
        res = app.exec();
    }  catch (const std::exception& e) {
        std::cerr << "Error detected!" << std::endl;
        std::cerr << e.what() << std::endl;
        std::cerr << "Abnormal closing of program." << std::endl;
    }

    return res;
}
