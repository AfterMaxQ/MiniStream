#include "macos/ui/client_controller.hpp"

#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>

int main(int argc, char* argv[]) {
  QGuiApplication application(argc, argv);
  QCoreApplication::setApplicationName(QStringLiteral("MiniStream"));
  QCoreApplication::setOrganizationName(QStringLiteral("AfterMaxQ"));

  ministream::ClientController controller;
  QQmlApplicationEngine engine;
  engine.rootContext()->setContextProperty(QStringLiteral("clientController"), &controller);
  engine.loadFromModule(QStringLiteral("MiniStream"), QStringLiteral("ClientMain"));
  if (engine.rootObjects().isEmpty()) {
    return 1;
  }
  return application.exec();
}
