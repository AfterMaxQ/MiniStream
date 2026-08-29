#include "windows/ui/host_controller.hpp"

#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>

int main(int argc, char* argv[]) {
  QGuiApplication application(argc, argv);
  QCoreApplication::setApplicationName(QStringLiteral("MiniStream Host"));
  QCoreApplication::setOrganizationName(QStringLiteral("AfterMaxQ"));

  ministream::HostController controller;
  QQmlApplicationEngine engine;
  engine.rootContext()->setContextProperty(QStringLiteral("hostController"), &controller);
  engine.loadFromModule(QStringLiteral("MiniStream"), QStringLiteral("HostMain"));
  if (engine.rootObjects().isEmpty()) {
    return 1;
  }
  return application.exec();
}
