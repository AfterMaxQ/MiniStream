# MiniStream UI style checklist

Use this checklist for every MiniStream UI change.

## Layout and color

- Use the spacing tokens 4, 8, 12, 16, 24, and 32.
- Use only the radius tokens 6, 10, and 14.
- Keep surfaces neutral. Use one accent color and the semantic success, warning, and error colors.
- Do not use gradients, glow effects, glassmorphism, or 3D decoration.
- Keep cards shallow; do not nest cards to communicate ordinary state.
- Keep the stream surface and its overlay in the same Qt Quick window.

## Motion and interaction

- Use the motion tokens 120, 180, and 220 ms for transitions.
- Keep the normal Allow control and Remote control paths to three primary actions or fewer.
- Make the current capability or connection state visible without opening a detail page.
- F11 toggles fullscreen. Esc exits fullscreen in local input mode; while remote input is active both keys are sent to the remote application.
- Keep a visible `Control remote` / `Use this device` input control. Do not install global keyboard or mouse hooks.
- Do not let UI work run on a media or network hot-path thread.

## Copy

- Describe the product state or the next action in plain language.
- Keep labels short: `Find devices`, `Connect`, `Allow control`, `Stop broadcast`, `Confirm`, `Cancel`, `Control remote`, and `Use this device`.
- Use specific failure text such as `Hardware HEVC decode unavailable` when a capability is missing.
- Do not use marketing claims, emoji as functional icons, or generic filler text.
- Run `python tools/check_ui_copy.py ui` before committing UI text.
