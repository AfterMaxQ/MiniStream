#include "app/ui/clipboard_bridge.hpp"
#include "core/session/clipboard_channel.hpp"
#include <QGuiApplication>
#include <QClipboard>
#include <QMimeData>
#ifdef __APPLE__
#import <AppKit/AppKit.h>
#endif

namespace ministream {
ClipboardBridge::ClipboardBridge(std::function<void(std::string)> sender, QObject* parent)
    : QObject(parent), sender_(std::move(sender)) {
  timer_.setInterval(250);
  connect(&timer_, &QTimer::timeout, this, [this] { poll(); });
  connect(QGuiApplication::clipboard(), &QClipboard::dataChanged, this, [this] { poll(); });
}
void ClipboardBridge::setActive(bool active, bool send_initial) {
  if (active_ == active) return;
  active_ = active;
  if (active) {
    last_text_ = QGuiApplication::clipboard()->text();
#ifdef __APPLE__
    change_count_ = [NSPasteboard generalPasteboard].changeCount;
#endif
    timer_.start();
    if (send_initial && QGuiApplication::clipboard()->mimeData()->hasText()) {
      const auto text = last_text_.toUtf8();
      if (text.size() <= static_cast<qsizetype>(ClipboardChannel::kMaxTextBytes)) sender_(text.toStdString());
    }
  } else timer_.stop();
}
void ClipboardBridge::poll() {
  if (!active_ || applying_) return;
  QString text;
#ifdef __APPLE__
  @autoreleasepool {
    NSPasteboard* board = [NSPasteboard generalPasteboard];
    if (change_count_ == board.changeCount) return;
    change_count_ = board.changeCount;
    NSString* value = [board stringForType:NSPasteboardTypeString];
    if (!value) return;
    NSData* bytes = [value dataUsingEncoding:NSUTF8StringEncoding];
    text = QString::fromUtf8(static_cast<const char*>(bytes.bytes), static_cast<qsizetype>(bytes.length));
  }
#else
  if (!QGuiApplication::clipboard()->mimeData()->hasText()) return;
  text = QGuiApplication::clipboard()->text();
#endif
  if (text == last_text_) return;
  last_text_ = text;
  const auto bytes = text.toUtf8();
  if (bytes.size() <= static_cast<qsizetype>(ClipboardChannel::kMaxTextBytes)) sender_(bytes.toStdString());
}
void ClipboardBridge::receive(const std::string& text) {
  if (!active_) return;
  last_text_ = QString::fromUtf8(text.data(), static_cast<qsizetype>(text.size()));
  applying_ = true;
  QGuiApplication::clipboard()->setText(last_text_);
#ifdef __APPLE__
  change_count_ = [NSPasteboard generalPasteboard].changeCount;
#endif
  applying_ = false;
}
}
