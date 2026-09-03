from pathlib import Path


ROOT = Path(__file__).parents[2]


def test_packaging_declares_local_network_and_private_firewall_contract() -> None:
    info = (ROOT / "packaging" / "macos" / "Info.plist.in").read_text(encoding="utf-8")
    packaging = (ROOT / "packaging" / "Packaging.cmake").read_text(encoding="utf-8")
    macos_cmake = (ROOT / "src" / "macos" / "CMakeLists.txt").read_text(encoding="utf-8")
    firewall_install = (ROOT / "packaging" / "windows" / "ministream_firewall_install.nsh").read_text(
        encoding="utf-8"
    )
    firewall_uninstall = (ROOT / "packaging" / "windows" / "ministream_firewall_uninstall.nsh").read_text(
        encoding="utf-8"
    )

    assert "NSLocalNetworkUsageDescription" in info
    assert "nearby devices for game streaming" in info
    assert "NSScreenCaptureUsageDescription" in info
    assert "shared display and system audio" in info
    assert "MACOSX_BUNDLE_INFO_PLIST" in macos_cmake
    assert "Contents/Frameworks" in packaging
    assert "install_name_tool" in packaging
    assert "profile=private" in firewall_install
    assert "protocol=UDP" in firewall_install
    assert "profile=public" not in firewall_install
    assert "delete rule name=\"MiniStream (Private UDP)\"" in firewall_uninstall
    assert 'set(CPACK_NSIS_MUI_FINISHPAGE_RUN "ministream.exe")' in packaging
    assert 'CPACK_NSIS_MUI_FINISHPAGE_RUN "bin' not in packaging


if __name__ == "__main__":
    test_packaging_declares_local_network_and_private_firewall_contract()
    print("Packaging contract check passed")
