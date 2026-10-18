# Symbolic Link Manager

A native Win32 GUI utility written in C++17 for creating file and directory symbolic links on Windows. The program validates both paths before creating a link and does not overwrite an existing destination.

## Project layout

```text
SymbolicLinkManager/
├── include/
│   └── mine.hpp       # Shared types and public declarations
├── src/
│   └── mine.cpp       # Implementation and Win32 GUI entry point
├── Test/
│   └── test.cpp       # Lightweight unit tests
├── CMakeLists.txt
├── .gitignore
└── README.md
```

## Requirements

- Windows 7 or later (Windows 10/11 is recommended).
- A C++17-capable compiler and CMake 3.20 or later.
- Windows SDK headers and libraries.

## Build the application

Run these commands from the project root in a Windows developer terminal:

```powershell
cmake -S . -B build -A x64
cmake --build build --config Release
```

The executable is created in `build/Release/SymbolicLinkManager.exe` with Visual Studio generators. Other CMake generators may use a different output path.

## Run tests

```powershell
ctest --test-dir build -C Release --output-on-failure
```

The tests cover path-input cleanup, path helper behavior, invalid link-name characters, and operation-result construction. Filesystem-dependent validation and actual symbolic-link creation are not exercised by these unit tests.

## Symbolic link permissions

Windows may require Developer Mode or administrator privileges, depending on the operating system configuration and policy. The application reports common errors and provides guidance where possible.

## Notes

- The project uses only the Win32 API and the C++ standard library.
- The application logs to `app.log.txt` beside the executable when writable, otherwise it attempts a per-user fallback location.
