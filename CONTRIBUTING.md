# Contributing to ShotSequence

Thank you for your interest in contributing to ShotSequence.

## Getting Started

1. Fork the repository.
2. Clone your fork.
3. Install the dependencies listed in README.md.
4. Create a branch for your changes.

## Building

    cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
    cmake --build build --parallel

## Testing

    ctest --test-dir build --output-on-failure

## Pull Requests

- Keep changes focused and reasonably sized.
- Explain what the change does and why.
- Test your changes before submitting.
- Update documentation when necessary.
- Do not include generated binaries, databases, credentials or personal files.
- Preserve existing functionality unless intentionally changing it.

For substantial changes, open an issue first to discuss the proposed approach.
