from pathlib import Path


UI_ROOT = Path(__file__).parents[2] / "ui"
ROLE_CONTROLLER = Path(__file__).parents[2] / "src" / "app" / "ui" / "role_controller.hpp"


def test_role_shell_copy_is_short_and_directional() -> None:
    main = (UI_ROOT / "Main.qml").read_text(encoding="utf-8")
    controlled = (UI_ROOT / "pages" / "ControlledPage.qml").read_text(encoding="utf-8")
    remote = (UI_ROOT / "pages" / "RemotePage.qml").read_text(encoding="utf-8")

    assert "Allow control" in controlled
    assert "Remote control" in remote
    assert "Allow control" in main
    assert "Remote control" in main
    for text in (remote,):
        assert "Find devices" in text
        assert "Nearby devices" in text
        assert "Connect" in text
        assert "Gamepad" not in text
        assert "Controller" not in text
    assert "Windows PCs" not in remote
    assert "This Mac" not in remote
    assert "roleController.setMode(1)" in main
    assert "roleController.setMode(2)" in main


def test_role_shell_has_bounded_mode_switch() -> None:
    source = (UI_ROOT / "Main.qml").read_text(encoding="utf-8")
    controller = ROLE_CONTROLLER.read_text(encoding="utf-8")
    assert "Math.min" in source
    assert "roleController.setMode(1)" in source
    assert "roleController.setMode(2)" in source
    assert "Q_INVOKABLE void setMode(int mode);" in controller


if __name__ == "__main__":
    test_role_shell_copy_is_short_and_directional()
    test_role_shell_has_bounded_mode_switch()
    print("Role shell copy check passed")
