from pathlib import Path


UI_ROOT = Path(__file__).parents[2] / "ui"
CLIENT_MAIN = UI_ROOT / "ClientMain.qml"
MAIN = UI_ROOT / "Main.qml"


def test_client_shortcuts_leave_game_keys_in_remote_mode() -> None:
    source = MAIN.read_text(encoding="utf-8")
    assert 'sequence: "Ctrl+Alt+R"' in source
    assert 'sequence: "Meta+Alt+R"' in source
    assert 'sequence: "Ctrl+Alt+F"' in source
    assert 'sequence: "Meta+Alt+F"' in source
    assert 'sequence: "Ctrl+Shift+F12"' not in source
    assert source.count("enabled: !roleController.remoteInputActive") >= 2


def test_legacy_client_shortcuts_remain_consistent() -> None:
    source = CLIENT_MAIN.read_text(encoding="utf-8")
    assert 'sequence: "Ctrl+Alt+R"' in source
    assert 'sequence: "Meta+Alt+R"' in source
    assert 'sequence: "Ctrl+Alt+F"' in source
    assert 'sequence: "Meta+Alt+F"' in source
    assert source.count("enabled: !clientController.remoteInputActive") >= 2


if __name__ == "__main__":
    test_client_shortcuts_leave_game_keys_in_remote_mode()
    test_legacy_client_shortcuts_remain_consistent()
    print("Shortcut policy check passed")
