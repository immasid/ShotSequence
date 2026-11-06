# ShotSequence

ShotSequence is a Qt 6 desktop application for reviewing and organizing photo and video collections using embedded capture-date metadata.

It helps organize mixed-media collections chronologically while keeping original source files untouched.

## Features

- Scans photo and video metadata for capture timestamps.
- Supports EXIF, XMP, IPTC and QuickTime metadata.
- Organizes photos and videos chronologically.
- Allows manual capture-date corrections.
- Provides custom output naming and sequence ordering.
- Supports drag-and-drop ordering.
- Uses SQLite to persist manual date corrections.
- Uses SHA-256 hashing for file integrity checks.
- Provides background scanning and image previews.
- Includes a dark Qt desktop interface.

## Requirements

- C++17-compatible compiler (GCC or Clang).
- CMake 3.16 or newer.
- Qt 6: Core, Gui, Widgets and Sql.
- ExifTool available on PATH.

### Fedora Dependencies

    sudo dnf install gcc-c++ cmake qt6-qtbase-devel perl-Image-ExifTool

## Build

Clone the repository:

    git clone https://github.com/immasid/ShotSequence.git
    cd ShotSequence

Configure and build:

    cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
    cmake --build build --parallel

Run:

    ./build/ShotSequence

## Quick Launch

    chmod +x run.sh
    ./run.sh

## Tests

    ctest --test-dir build --output-on-failure

The current automated tests cover capture-date parsing, metadata tag priority, natural sorting and ordering detection.

## Project Structure

    ShotSequence/
    ├── .github/
    ├── src/
    ├── tests/
    ├── CMakeLists.txt
    ├── CONTRIBUTING.md
    ├── LICENSE
    ├── README.md
    ├── SECURITY.md
    └── run.sh

## Data Safety

ShotSequence is designed to copy selected media into destination folders instead of modifying original source files.

When manual capture-date correction is requested, metadata changes are applied to the organized destination copy.

Always maintain independent backups of important media.

## Platform Support

The project has been built and launched successfully on Fedora Linux with Qt 6.

Other operating systems require separate build and runtime validation before compatibility can be claimed.

## License

Distributed under the MIT License. See LICENSE for details.
