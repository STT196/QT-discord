#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QProcessEnvironment>
#include <QUrl>
#include <QDebug>
#include <QtGlobal>
#include <cstdio>
#include "discordclient.h"

static void messageHandler(QtMsgType type, const QMessageLogContext &, const QString &msg) {
    QByteArray localMsg = msg.toLocal8Bit();
    fprintf(stderr, "%s\n", localMsg.constData());
    fflush(stderr);
}

int main(int argc, char **argv) {
    qInstallMessageHandler(messageHandler);

    QGuiApplication app(argc, argv);
    qputenv("QT_QUICK_CONTROLS_STYLE", "Basic");

    QString token = QProcessEnvironment::systemEnvironment().value("DISCORD_TOKEN");
    bool useBotPrefix = true;
    int intents = 513;
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

    // Auto-login only if a token was supplied; otherwise the QML login page is shown.
    if (!token.isEmpty())
        client.login(token, useBotPrefix);

    return app.exec();
}
