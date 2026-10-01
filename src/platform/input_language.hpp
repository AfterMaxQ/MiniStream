#pragma once
#include <memory>

namespace ministream {
class InputLanguageGuard {
 public:
  explicit InputLanguageGuard(bool foreground = false);
  ~InputLanguageGuard();
  void setEnglish(bool enabled);
  void refresh();
 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};
}
