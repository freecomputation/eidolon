# Eidolon - build toolchain setup

Verified end to end on this VM on 2026-11-10: a test app compiled against the
official Flipper firmware 1.5.1 SDK and the produced .fap was decoded and
confirmed to target API 88.2, matching the user's Flipper (official 1.5.1).

## ufbt installation

- `pip install --user ufbt` is blocked on this VM by PEP 668
  (externally-managed system Python), so ufbt lives in a venv instead:
  - `python3 -m venv ~/workspace/eidolon/.venv`
  - `~/workspace/eidolon/.venv/bin/pip install ufbt` (installed ufbt 0.2.6)
- ufbt binary: `~/workspace/eidolon/.venv/bin/ufbt`
- ufbt's own state persists under `~/.ufbt/`:
  - SDK: `~/.ufbt/current` (46 MB)
  - ARM toolchain: `~/.ufbt/toolchain` (1.2 GB)

## SDK

- Source: official update index `https://update.flipperzero.one/firmware/directory.json`,
  channel `release`, deployed by ufbt's first-run bootstrap
  (`ufbt status` reports Mode: channel, Version: 1.5.1, Target: f7).
- No version pin was needed: the release channel currently serves firmware
  1.5.1 exactly. If a future release moves past it, re-pin with
  `ufbt update --channel release` before it lands, or use ufbt's
  `--url <sdk zip url>` / `--local <sdk zip>` options to fix the SDK.
- SDK API version proven: 88.2 (see verification below).

## Build

From the app source dir (`application.fam` + `eidolon.c`):

    cd ~/workspace/eidolon/app
    ~/workspace/eidolon/.venv/bin/ufbt

Output: `~/workspace/eidolon/app/dist/eidolon.fap` (2,316 bytes).
The test app is a minimal GUI program (ViewPort on the fullscreen GUI layer)
that draws "EIDOLON" and exits on Back; appid `eidolon`, name "Eidolon",
category Tools, stack 2048, version 1.0.

## Verification (from the artifact, not the exit code)

- ufbt's own APPCHK step printed `Target: 7, API: 88.2`.
- The .fap's `.fapmeta` ELF section was decoded with the SDK's manifest
  layout (`~/.ufbt/current/scripts/fbt/elfmanifest.py`): magic 0x52474448,
  manifest version 1, api_version field 0x00580002 = API 88.2,
  hardware target 7 (Flipper Zero), app name field "Eidolon".
- `strings` on the .fap contains both "Eidolon" (manifest name) and
  "EIDOLON" (the drawn screen title).
