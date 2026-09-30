#include "app/ui/control_escape_filter.hpp"

#include <catch2/catch_test_macros.hpp>
#include <QCoreApplication>

using namespace ministream;

namespace {
class GameFocus final : public QObject {
 public:
  unsigned keys{};
  bool event(QEvent* event) override {
    if (event->type() == QEvent::KeyPress || event->type() == QEvent::KeyRelease) {
      ++keys;
      return true;
    }
    return QObject::event(event);
  }
};
}

TEST_CASE("control escape beats a focused game item and consumes the release key") {
  int argc = 1;
  char name[] = "escape-test";
  char* argv[] = {name, nullptr};
  QCoreApplication app(argc, argv);
  GameFocus game;
  bool remote = true;
  unsigned releases{};
  ControlEscapeFilter filter([&] { return remote; }, [&] { remote = false; ++releases; });
  auto modifiers = Qt::ControlModifier | Qt::AltModifier | Qt::ShiftModifier;
  QKeyEvent override_event(QEvent::ShortcutOverride, Qt::Key_R, modifiers);
  override_event.ignore();
  QCoreApplication::sendEvent(&game, &override_event);
  REQUIRE(override_event.isAccepted());
  REQUIRE(remote);
  QKeyEvent press(QEvent::KeyPress, Qt::Key_R, modifiers);
  QCoreApplication::sendEvent(&game, &press);
  REQUIRE_FALSE(remote);
  REQUIRE(releases == 1);
  QKeyEvent repeat(QEvent::KeyPress, Qt::Key_R, modifiers, {}, true);
  QCoreApplication::sendEvent(&game, &repeat);
  QKeyEvent release(QEvent::KeyRelease, Qt::Key_R, Qt::NoModifier);
  QCoreApplication::sendEvent(&game, &release);
  REQUIRE(game.keys == 0);
  REQUIRE(releases == 1);
  for (auto key : {Qt::Key_Escape, Qt::Key_F11, Qt::Key_R}) {
    QKeyEvent plain(QEvent::KeyPress, key, Qt::NoModifier);
    QCoreApplication::sendEvent(&game, &plain);
  }
  REQUIRE(game.keys == 3);
#ifdef __APPLE__
  QCoreApplication::setAttribute(Qt::AA_MacDontSwapCtrlAndMeta, true);
  remote = true;
  QKeyEvent mac_command(QEvent::KeyPress, Qt::Key_R,
                       Qt::MetaModifier | Qt::AltModifier | Qt::ShiftModifier);
  QCoreApplication::sendEvent(&game, &mac_command);
  REQUIRE_FALSE(remote);
  REQUIRE(releases == 2);
  QCoreApplication::setAttribute(Qt::AA_MacDontSwapCtrlAndMeta, false);
#endif
}
