#pragma once
#include <QCoreApplication>
#include <QKeyEvent>
#include <QObject>
#include <functional>
#include <utility>

namespace ministream {
class GameKeyboardFilter : public QObject {
 public:
  GameKeyboardFilter(std::function<bool()> active,
                     std::function<void(const QKeyEvent&, bool)> route, QObject* parent)
      : QObject(parent), active_(std::move(active)), route_(std::move(route)) {
    QCoreApplication::instance()->installEventFilter(this);
  }
 protected:
  bool eventFilter(QObject*, QEvent* event) override {
    if (!active_()) return false;
    if (event->type() == QEvent::InputMethod) return true;
    if (event->type() != QEvent::ShortcutOverride && event->type() != QEvent::KeyPress &&
        event->type() != QEvent::KeyRelease) return false;
    auto* key = static_cast<QKeyEvent*>(event);
    if (event->type() != QEvent::ShortcutOverride && !key->isAutoRepeat())
      route_(*key, event->type() == QEvent::KeyPress);
    key->accept();
    return true;
  }
 private:
  std::function<bool()> active_;
  std::function<void(const QKeyEvent&, bool)> route_;
};
}
