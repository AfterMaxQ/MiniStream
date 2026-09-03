import re
from pathlib import Path


ROOT = Path(__file__).parents[2]


def test_release_version_is_derived_from_project_version() -> None:
    cmake = (ROOT / "CMakeLists.txt").read_text(encoding="utf-8")
    packaging = (ROOT / "packaging" / "Packaging.cmake").read_text(encoding="utf-8")
    match = re.search(r"project\(MiniStream\s+VERSION\s+([0-9.]+)", cmake)
    assert match, "project version declaration is missing"
    assert match.group(1) == "0.2.3"
    assert 'set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")' in packaging
    assert 'set(CPACK_PACKAGE_FILE_NAME "MiniStream-${PROJECT_VERSION}-' in packaging


if __name__ == "__main__":
    test_release_version_is_derived_from_project_version()
    print("Version contract check passed")
