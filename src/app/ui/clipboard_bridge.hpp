#pragma once
#include <QObject>
#include <QTimer>
#include <QString>
#include <functional>
#include <string>

namespace ministream {
class ClipboardBridge : public QObject {
 public:
  ClipboardBridge(std::function<void(std::string)> sender, QObject* parent);
  void setActive(bool active, bool send_initial);
  void receive(const std::string& text);
 private:
  void poll();
  std::function<void(std::string)> sender_;
  QTimer timer_;
  QString last_text_;
  bool active_{};
  bool applying_{};
#ifdef __APPLE__
  long change_count_{-1};
#endif
};
}
