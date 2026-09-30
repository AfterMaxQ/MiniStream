#pragma once

#include <QCoreApplication>
#include <QKeyEvent>
#include <QObject>

#include <functional>
#include <utility>

namespace ministream {

// Application filters run before focused QML items can consume game keys.
class ControlEscapeFilter final : public QObject {
 public:
  ControlEscapeFilter(std::function<bool()> active, std::function<void()> release,
                      QObject* parent = nullptr)
      : QObject(parent), active_(std::move(active)), release_(std::move(release)) {
    QCoreApplication::instance()->installEventFilter(this);
  }

 protected:
  bool eventFilter(QObject*, QEvent* event) override {
    if (event->type() != QEvent::ShortcutOverride && event->type() != QEvent::KeyPress &&
        event->type() != QEvent::KeyRelease) return false;
    auto* key = static_cast<QKeyEvent*>(event);
    if (key->key() != Qt::Key_R) return false;
    if (swallow_r_) {
      if (event->type() == QEvent::KeyRelease && !key->isAutoRepeat()) swallow_r_ = false;
      key->accept();
      return true;
    }
    auto command = Qt::ControlModifier;
#ifdef __APPLE__
    if (QCoreApplication::testAttribute(Qt::AA_MacDontSwapCtrlAndMeta)) command = Qt::MetaModifier;
#endif
    const auto required = command | Qt::AltModifier | Qt::ShiftModifier;
    if (!active_() || (key->modifiers() & required) != required) return false;
    key->accept();
    if (event->type() == QEvent::KeyPress) {
      swallow_r_ = true;
      release_();
    }
    return true;
  }

 private:
  std::function<bool()> active_;
  std::function<void()> release_;
  bool swallow_r_{};
};

}  // namespace ministream
