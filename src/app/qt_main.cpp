#include "app/ui/role_controller.hpp"
#include "app/ui/video_surface_item.hpp"
#include "app/ui/relative_mouse_capture.hpp"

#include <QCoreApplication>
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QtQml/qqml.h>
#include <QQuickWindow>
#include <QSGRendererInterface>
#include <QIcon>

int main(int argc, char* argv[]) {
  // Qt falls back to SDR when the current output cannot create an HDR swapchain.
  qputenv("QSG_RHI_HDR", "scrgb");
#ifdef __APPLE__
  QCoreApplication::setAttribute(Qt::AA_MacDontSwapCtrlAndMeta);
  QQuickWindow::setGraphicsApi(QSGRendererInterface::Metal);
#elif defined(_WIN32)
  QQuickWindow::setGraphicsApi(QSGRendererInterface::Direct3D11);
#endif
  QGuiApplication application(argc, argv);
  QCoreApplication::setApplicationName(QStringLiteral("MiniStream"));
  QCoreApplication::setOrganizationName(QStringLiteral("AfterMaxQ"));
  QGuiApplication::setWindowIcon(QIcon(QStringLiteral(":/icons/ministream.png")));

  ministream::RoleController controller;
  qmlRegisterType<ministream::VideoSurfaceItem>("MiniStream", 1, 0,
                                                "VideoSurfaceItem");
  qmlRegisterType<ministream::RelativeMouseCapture>("MiniStream", 1, 0, "RelativeMouseCapture");
  QQmlApplicationEngine engine;
  engine.rootContext()->setContextProperty(QStringLiteral("roleController"), &controller);
  engine.loadFromModule(QStringLiteral("MiniStream"), QStringLiteral("Main"));
  if (engine.rootObjects().isEmpty()) {
    return 1;
  }
  return application.exec();
}
