from pathlib import Path
import json
import plistlib
import re


ROOT = Path(__file__).parents[2]


def test_packaging_declares_local_network_and_private_firewall_contract() -> None:
    info = plistlib.loads((ROOT / "packaging" / "macos" / "Info.plist.in").read_bytes())
    packaging = (ROOT / "packaging" / "Packaging.cmake").read_text(encoding="utf-8")
    firewall_install = (ROOT / "packaging" / "windows" / "ministream_firewall_install.nsh").read_text(
        encoding="utf-8"
    )
    firewall_uninstall = (ROOT / "packaging" / "windows" / "ministream_firewall_uninstall.nsh").read_text(
        encoding="utf-8"
    )

    assert info["NSLocalNetworkUsageDescription"].strip()
    assert info["NSScreenCaptureUsageDescription"].strip()
    assert info["CFBundleShortVersionString"] == "${MACOSX_BUNDLE_SHORT_VERSION_STRING}"
    project = (ROOT / "CMakeLists.txt").read_text(encoding="utf-8")
    version = re.search(r"project\(MiniStream\s+VERSION\s+([0-9.]+)", project)
    assert version and re.fullmatch(r"\d+\.\d+\.\d+(?:\.\d+)?", version.group(1))
    def setting(name: str) -> str:
        match = re.search(r"set\(\s*" + re.escape(name) + r'\s+"?([^"\s)]+)', packaging)
        assert match, f"Missing package setting: {name}"
        return match.group(1)
    assert setting("CPACK_PACKAGE_VERSION") == "${PROJECT_VERSION}"
    presets = json.loads((ROOT / "CMakePresets.json").read_text(encoding="utf-8"))
    desktop = next(p for p in presets["configurePresets"] if p["name"] == "desktop")
    assert desktop["cacheVariables"]["MINISTREAM_ENABLE_PACKAGING"] == "ON"
    assert set(re.findall(r"profile=(\w+)", firewall_install, re.IGNORECASE)) == {"private"}
    assert set(re.findall(r"protocol=(\w+)", firewall_install, re.IGNORECASE)) == {"UDP"}
    installed_rules = set(re.findall(r'add\s+rule\s+name="([^"]+)"', firewall_install))
    removed_rules = set(re.findall(r'delete\s+rule\s+name="([^"]+)"', firewall_uninstall))
    assert installed_rules and installed_rules <= removed_rules
    assert setting("CPACK_NSIS_MUI_FINISHPAGE_RUN") == "ministream.exe"


if __name__ == "__main__":
    test_packaging_declares_local_network_and_private_firewall_contract()
    print("Packaging contract check passed")
