# ShredEQ

Zero-latency adaptive parametric, dynamic, and multiband EQ for guitar/bass.
Developed by nSpace Systems.

Built with C++20 and JUCE, supporting VST3 and Standalone formats.

## Build Instructions

1. Initialize submodules:
   ```bash
   git submodule update --init --recursive
   ```

2. Configure and build using CMake presets:
   ```bash
   cmake --preset <your-preset>
   cmake --build --preset <your-build-preset>
   ```
   Alternatively, use the provided build script:
   ```bash
   scripts/build_all.sh
   ```

## Documentation

Full technical specifications are located in `SHREDEQ_SPEC.md` and the `.abracapocus` state.
