# MiniStream UI style checklist

Use this checklist for every Host or Client UI change.

## Layout and color

- Use the spacing tokens 4, 8, 12, 16, 24, and 32.
- Use only the radius tokens 6, 10, and 14.
- Keep surfaces neutral. Use one accent color and the semantic success, warning, and error colors.
- Do not use gradients, glow effects, glassmorphism, or 3D decoration.
- Keep cards shallow; do not nest cards to communicate ordinary state.
- Keep the stream surface and its overlay in the same Qt Quick window.

## Motion and interaction

- Use the motion tokens 120, 180, and 220 ms for transitions.
- Keep the normal Host and Client paths to three primary actions or fewer.
- Make the current capability or connection state visible without opening a detail page.
- F11 toggles fullscreen. Esc releases remote input and exits fullscreen.
- Keep a visible local/remote input control; `Ctrl+Shift+F12` is the keyboard escape hatch.
- Do not let UI work run on a media or network hot-path thread.

## Copy

- Describe the product state or the next action in plain language.
- Keep labels short: `Find PCs`, `Connect`, `Start Host`, `Stop Host`, `Confirm`, and `Cancel`.
- Use specific failure text such as `Hardware HEVC decode unavailable` when a capability is missing.
- Do not use marketing claims, emoji as functional icons, or generic filler text.
- Run `python tools/check_ui_copy.py ui` before committing UI text.
