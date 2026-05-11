# Building

## Prerequisites

- Visual Studio 2022 Build Tools (or Visual Studio 2022)
- CMake

## Open a Developer Shell

```bat
"C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\Tools\VsDevCmd.bat"
```

## Configure & Build

```bat
cmake -S . -B build-fresh
cmake --build build-fresh --config Release --target shared_km
```

The output EXE will be at `build-fresh/apps/Release/shared_km.exe`.

## Run

Run the EXE directly (no command-line arguments = UI mode):

```bat
build-fresh\apps\Release\shared_km.exe
```

### Command-line flags

- `--receiver` or `-r` — start in receiver-only console mode
- `--sender` or `-s` — start in sender-only console mode

## Test

```bat
ctest --test-dir build-fresh --output-on-failure -C Release
```

## Notes

- The project uses **UDP** for transport (not TCP).
- Low-level mouse/keyboard hooks require appropriate privileges.
- Input injection (`SendInput`) may require administrator privileges on some systems.
