# ProtoGL GPU Firmwares

This directory contains GPU-side firmware implementations for each supported
target architecture. Each subfolder is a **standalone project** that builds
independently of the ESP32-S3 host PlatformIO project.

All firmwares share the same ProtoGL wire protocol and can be discovered by
the host at runtime via the I2C capability query (register `0x09`).

## Targets

| Directory | GPU | Build System | Status |
|---|---|---|---|
| `rp2350/` | RP2350 (ARM Cortex-M33, dual-core) | Pico-SDK + CMake | **Phase 1 — Active** |

Future targets will be added as subdirectories here:

- `rp2350_riscv/` — RP2350 in Hazard3 RISC-V boot mode (recompile of `rp2350/`)
- `riscv_custom/`  — Custom RISC-V SoC GPU
- `fpga/`          — FPGA-based hardware rasterizer
- `stm32h7/`       — ARM Cortex-M7 (STM32H7)

## Building

Each firmware directory contains its own `CMakeLists.txt` (or equivalent).
See the target's `README.md` for build instructions.

### RP2350 Quick Start

```bash
cd rp2350
mkdir build && cd build
cmake .. -DPICO_SDK_PATH=/path/to/pico-sdk
make -j$(nproc)
```

The resulting `.uf2` file can be flashed via USB drag-and-drop (BOOTSEL mode).

## Shared Code

All firmwares use the shared ProtoGL header files from `../src/`:
- `PglTypes.h`   — Wire-format structs and enums
- `PglOpcodes.h` — Command opcode constants
- `PglParser.h`  — Alignment-safe deserialization (safe on RISC-V, ARM, FPGA)
- `PglCRC16.h`   — CRC-16/CCITT-FALSE integrity check

The firmware does **not** use `PglEncoder.h` or `PglDevice.h` — those are
host-side only.
