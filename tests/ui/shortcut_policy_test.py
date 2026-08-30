from pathlib import Path


CLIENT_MAIN = Path(__file__).parents[2] / "ui" / "ClientMain.qml"


def test_client_shortcuts_leave_game_keys_in_remote_mode() -> None:
    source = CLIENT_MAIN.read_text(encoding="utf-8")
    assert 'sequence: "Ctrl+Alt+R"' in source
    assert 'sequence: "Meta+Alt+R"' in source
    assert 'sequence: "Ctrl+Alt+F"' in source
    assert 'sequence: "Meta+Alt+F"' in source
    assert 'sequence: "Ctrl+Shift+F12"' not in source
    assert source.count("enabled: !clientController.remoteInputActive") >= 2


if __name__ == "__main__":
    test_client_shortcuts_leave_game_keys_in_remote_mode()
    print("Shortcut policy check passed")
