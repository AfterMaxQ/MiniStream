from pathlib import Path


ROOT = Path(__file__).parents[2]
WORKFLOW = ROOT / ".github" / "workflows" / "ci.yml"


def test_ci_builds_and_tests_both_desktop_platforms() -> None:
    source = WORKFLOW.read_text(encoding="utf-8")

    assert "runs-on: windows-2022" in source
    assert "runs-on: macos-14" in source
    assert source.count("MINISTREAM_BUILD_UI=OFF") == 2
    assert source.count("--target ministream_tests") == 2
    assert source.count("ctest --test-dir build") == 2
    assert "jurplel/install-qt-action@v4" in source
    assert "brew install libsodium" in source


if __name__ == "__main__":
    test_ci_builds_and_tests_both_desktop_platforms()
    print("CI workflow contract check passed")
