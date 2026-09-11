#include "MainWindow.h"
#include <QApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStandardPaths>
#include <QDateTime>
#include <QMutex>
#include <QTimer>

namespace {
QString logPath;
QMutex logMutex;
void logMessage(QtMsgType, const QMessageLogContext &, const QString &message)
{
    QMutexLocker lock(&logMutex);
    QFile file(logPath);
    if (file.open(QIODevice::WriteOnly | QIODevice::Append))
        file.write((QDateTime::currentDateTime().toString(Qt::ISODate) + " " + message + "\n").toUtf8());
}
}
int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    QCoreApplication::setOrganizationName("Keishin");
    QCoreApplication::setApplicationName("mediaPlayer");
    const QString logDir = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    QDir().mkpath(logDir);
    logPath = logDir + "/player.log";
    if (QFileInfo(logPath).size() > 2 * 1024 * 1024) QFile::remove(logPath);
    qInstallMessageHandler(logMessage);
    MainWindow window;
    window.show();
    if (app.arguments().size() > 1) {
        const QString path = app.arguments().at(1);
        QTimer::singleShot(0, &window, [&window, path] { window.openFile(path); });
    }
    return app.exec();
}
