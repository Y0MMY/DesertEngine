# DesertEngine

[![CI](https://github.com/Y0MMY/DesertEngine/actions/workflows/ci.yml/badge.svg?branch=dev)](https://github.com/Y0MMY/DesertEngine/actions/workflows/ci.yml)

C++20 / Vulkan game engine with an ImGui editor, Lua gameplay scripting, a custom shader language,
a project system (Project Hub) and a standalone Runtime player. Runs on macOS (Apple Silicon,
MoltenVK) and Windows.

## Quick start (macOS)

```bash
./scripts/MacOS/Setup.sh              # one-time: brew deps + third-party fetch
./scripts/MacOS/BuildMacOS.sh Debug   # build everything (Editor, Runtime, ProjectHub, tests)
./scripts/MacOS/RunProjectHub.sh      # pick/create a project, launches the Editor
```

Direct launches:

```bash
./scripts/MacOS/RunEditor.sh  Debug [--project path/to/Game.deproj]   # no args = built-in sandbox
./scripts/MacOS/RunRuntime.sh Debug [--project ...] [--scene ...]     # standalone player (Play mode)
./scripts/MacOS/RunTests.sh   "$PWD" Debug                            # unit tests
```

## CI

Every push/PR builds Debug + Release and runs the full test suite on Apple Silicon macOS
(`.github/workflows/ci.yml`).
