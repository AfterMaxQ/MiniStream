#include "app/ui/role_controller.hpp"
#include "app/ui/video_surface_item.hpp"

#include <QCoreApplication>
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QtQml/qqml.h>

int main(int argc, char* argv[]) {
  QGuiApplication application(argc, argv);
  QCoreApplication::setApplicationName(QStringLiteral("MiniStream"));
  QCoreApplication::setOrganizationName(QStringLiteral("AfterMaxQ"));

  ministream::RoleController controller;
  qmlRegisterType<ministream::VideoSurfaceItem>("MiniStream", 1, 0,
                                                "VideoSurfaceItem");
  QQmlApplicationEngine engine;
  engine.rootContext()->setContextProperty(QStringLiteral("roleController"), &controller);
  engine.loadFromModule(QStringLiteral("MiniStream"), QStringLiteral("Main"));
  if (engine.rootObjects().isEmpty()) {
    return 1;
  }
  return application.exec();
}
