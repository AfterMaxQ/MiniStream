# Repository guidance

## Scope and documentation

- Before editing, identify the requested outcome, allowed files, preserved files, public/private boundary, and verification method.
- Inspect the tracked repository before describing its structure. Document only current public files and stable build outputs supported by tracked configuration.
- Keep README.md focused on installation, capabilities, and use. Put session progress, review notes, local paths, and handoff instructions in the response or commit message.
- Make surgical documentation edits. Do not invent assets or add unrelated examples, historical notes, or defensive commentary.
- Before completion, inspect `git diff --name-status` and the complete documentation diff; remove unrequested content and ensure every public statement remains supported by the repository.

## Test maintenance

- Maintain tests by module and public behavior. Search existing coverage before adding a file or case; extend or rewrite the owning test instead of appending a test for every patch.
- Remove obsolete assertions, duplicate cases, unused fixtures, and superseded test scripts when behavior changes. Update CMake and CI registrations in the same change.
- Prefer observable behavior through public APIs. Do not test C++/QML source substrings, variable names, exact UI copy, implementation line counts, or the current release number. Parse public configuration formats when testing packaging contracts.
- Use tables or sections for variants of one contract. Add a new test file only for a distinct responsibility that has no suitable existing home.
- Keep protocol round trips, malformed inputs, input release, reconnects, bounded queues, and failure recovery covered. Do not weaken these assertions to make a change pass.
- Make asynchronous tests wait for observable state with a bounded deadline. Centralize shared pumping helpers; do not fix races with arbitrary additional sleeps or larger retry counts.
- Isolate network tests to loopback with their own endpoints and ports. Hardware-dependent tests must remain explicitly tagged and separate from deterministic tests.
- Derive versions from project configuration; update golden wire bytes only when the public protocol changes.
- Do not write tests that merely mirror an implementation. A regression case must demonstrate a distinct failure or protect a stable contract.
- Run the affected tests when verification is authorized. If the user requests skipping tests, preserve and maintain coverage, report that execution was skipped, and do not claim the tests pass.

## Changes and releases

- Start with `git status --short` and preserve unrelated local changes. Stage named files; do not reset, clean, force-push, or overwrite existing release assets.
- Keep native Windows and macOS behavior aligned. Distinguish build/package evidence from hardware or two-device runtime evidence.
- Respect the user's requested commit count, release version, verification scope, and delegation preferences.
