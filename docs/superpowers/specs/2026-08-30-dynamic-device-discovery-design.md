# Dynamic Device Discovery and Control Roles

**Status:** Draft for review  
**Date:** 2026-08-30

## Goal

Make the control direction visible without tying it to an operating system. A
device that the user explicitly allows to be controlled advertises itself on
the LAN. A controller discovers that advertisement, shows the advertised
system identity and stream capabilities, and starts pairing from the selected
device. The role is selected for each connection and is not stored as a
permanent Windows/macOS mapping.

## Discovery contract

The shared discovery advertisement will carry bounded, versioned metadata:

- system type (a short enum rendered as the actual system name);
- user device name (the current system device name in the first version);
- session port;
- advertised codec capabilities;
- maximum width, height, and frame rate;
- HDR and audio capability flags;
- input capability flags for connection checks (not shown in the device row);
- a `controllable` role flag.

Only a device with `controllable` enabled answers discovery queries. The
existing discovery size limit remains enforced; invalid lengths, empty names,
zero ports, unknown flags, and unsupported protocol versions are rejected.
The advertisement remains a LAN hint only; pairing and all media packets keep
their existing authenticated handshake and encryption.

## Controlled-device flow

The home page starts in a non-broadcasting state. The user chooses **Allow
control** to run the capture/audio/controller readiness check and begin
answering LAN discovery queries. The page then shows **Visible on local
network** and a **Stop broadcast** action. Stopping the broadcast immediately
withdraws the device from discovery, releases input leases, and tears down an
active session.

If readiness is incomplete, broadcasting cannot start and the page shows the
specific missing capability. No platform name is used as a role label.

## Controller flow

The controller page presents **Nearby devices**. Each row is rendered only
from advertisement data:

```text
<system type> · <device name>
<codec> · <width>×<height> <fps> fps · <HDR> · <audio>
```

Input capabilities are not rendered as a required parameter. A session may use
keyboard and mouse without a gamepad, so the discovery card does not display a
gamepad field or imply that a controller is present.

Long names and parameter strings stay within the card by using a bounded
layout, wrapping the parameter line, and eliding the identity line at the
available width. The list never relies on a fixed card height; its height is
derived from its content and remains clipped by the scroll container.

Selecting a row starts the existing hello/accept exchange. The pairing page
shows the selected system identity and six-digit comparison code. After both
peers confirm, the stream page title is **Controlling · <system type> · <device
name>** and the input action is **Use this device** while remote input is
active. The reverse state is **Input stays local**.

## Session direction and cleanup

The hello and pairing transcript bind the advertised role and device identity
to the session. A peer that is not advertising as controllable cannot be used
as a target. The existing escape, shortcut, disconnect, close, and object
destructor paths continue to release every input lease. No global input hook
is added.

## Implementation boundary

Keep the current platform capture, audio, controller, and Qt entry points.
Extend the shared discovery model and the two application controllers instead
of introducing a new service framework. Platform backends continue to report
their real capabilities; a missing backend prevents **Allow control** rather
than being hidden by a software fallback.

## Verification

- Add wire tests for the new advertisement fields, bounds, role filtering, and
  round-trip identity/parameters.
- Add controller tests for start/stop broadcast, target selection, and cleanup
  on disconnect.
- Run the UI copy checker and verify long identity/parameter strings at the
  minimum window size and after resizing.
- Run the existing full CTest suite and the Windows hardware probe once after
  implementation.

## Non-goals

- Permanent controller/target assignments;
- Internet discovery or NAT traversal;
- exposing raw hardware identifiers or local filesystem paths;
- changing the media codec or encryption protocol beyond the role metadata
  needed to bind the selected target.
