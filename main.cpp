#include <QCoreApplication>
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QProcessEnvironment>
#include <QUrl>
#include <QDebug>
#include <QtGlobal>
#include <QWindow>
#include <cstdio>
#ifdef Q_OS_WIN
#include <windows.h>
#endif
#include "discordclient.h"
#include "voiceclient.h"
#include "voiceaudio.h"

static void messageHandler(QtMsgType type, const QMessageLogContext &, const QString &msg) {
    QByteArray localMsg = msg.toLocal8Bit();
    fprintf(stderr, "%s\n", localMsg.constData());
    fflush(stderr);
}

int main(int argc, char **argv) {
    qInstallMessageHandler(messageHandler);

    for (int i = 1; i < argc; ++i) {
        if (QString(argv[i]) == QStringLiteral("--voice-selftest"))
            return VoiceClient::selfTest() ? 0 : 1;
    }

    QCoreApplication::setOrganizationName("qdiscord");
    QCoreApplication::setOrganizationDomain("qdiscord.local");
    QCoreApplication::setApplicationName("qdiscord");

    QGuiApplication app(argc, argv);
    qputenv("QT_QUICK_CONTROLS_STYLE", "Basic");

    for (int i = 1; i < argc; ++i) {
        if (QString(argv[i]) == QStringLiteral("--voice-audio-selftest")) {
            VoiceAudio audio;
            const bool ok = audio.loopbackTest();
            fprintf(stderr, "[voice-audio-selftest] %s\n", ok ? "ok" : "failed");
            return ok ? 0 : 1;
        }
    }

    QString token = QProcessEnvironment::systemEnvironment().value("DISCORD_TOKEN");
    bool useBotPrefix = true;
    int intents = 641;
    for (int i = 1; i < argc; ++i) {
        QString a = argv[i];
        if (a == "--token" && i + 1 < argc)
            token = argv[++i];
        else if (a == "--user")
            useBotPrefix = false;
        else if (a == "--intents" && i + 1 < argc) {
            bool ok = false;
            int v = QString(argv[++i]).toInt(&ok);
            if (ok) intents = v;
            else fprintf(stderr, "Ignoring non-numeric --intents value\n");
        }
    }
    DiscordClient client;
    client.setIntents(intents);

    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty("discordClient", &client);
    engine.load(QUrl(QStringLiteral("qrc:/qml/qml/main.qml")));

    if (engine.rootObjects().isEmpty())
        return -1;

    // Flash the taskbar when a new message arrives while the window isn't focused.
    QWindow *win = qobject_cast<QWindow *>(engine.rootObjects().first());
    QObject::connect(&client, &DiscordClient::messageReceived,
                     [win](const QString &, const QString &, const QString &) {
                         if (!win || win->isActive())
                             return;
#ifdef Q_OS_WIN
                         FLASHWINFO fi;
                         fi.cbSize = sizeof(fi);
                         fi.hwnd = reinterpret_cast<HWND>(win->winId());
                         fi.dwFlags = FLASHW_ALL | FLASHW_TIMERNOFG;
                         fi.uCount = 0;
                         fi.dwTimeout = 0;
                         FlashWindowEx(&fi);
#else
                         QGuiApplication::alert(win, 1500);
#endif
                     });

    // Auto-login only if a token was supplied; otherwise the QML login page is shown.
    if (!token.isEmpty())
        client.login(token, useBotPrefix);

    return app.exec();
}
