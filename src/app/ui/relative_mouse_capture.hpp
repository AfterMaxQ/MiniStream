#pragma once

#include <QAbstractNativeEventFilter>
#include <QPointF>
#include <QPointer>
#include <QWindow>

namespace ministream {

class RelativeMouseCapture : public QObject, public QAbstractNativeEventFilter {
  Q_OBJECT
  Q_PROPERTY(QWindow* window READ window WRITE setWindow NOTIFY windowChanged)
  Q_PROPERTY(bool active READ active WRITE setActive NOTIFY activeChanged)
 public:
  explicit RelativeMouseCapture(QObject* parent = nullptr);
  ~RelativeMouseCapture() override;
  QWindow* window() const { return window_; }
  void setWindow(QWindow* window);
  bool active() const { return active_; }
  void setActive(bool active);
  bool nativeEventFilter(const QByteArray& type, void* message, qintptr*) override;
 signals:
  void windowChanged();
  void activeChanged();
  void moved(int dx, int dy);
  void captureFailed();
 private:
  QPointer<QWindow> window_;
  QPoint restore_position_;
  QPointF remainder_;
  bool active_{};
};

}  // namespace ministream
