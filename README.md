# nvidia-offset-controller
Small scripts to control Nvidia GPUs frequency offsets.

This repository provides tools for monitoring and dynamically adjusting NVIDIA GPU clock offsets to optimize performance and stability, particularly on Linux.

## Tools

### 1. `gpu_offset_control_v2` (Python)
The primary and most advanced tool in this repository. It dynamically adjusts GPU clock offsets based on temperature, power consumption, and current frequency using the NVIDIA Management Library (NVML).

**Features:**
- Dynamic offset adjustment based on real-time GPU telemetry.
- Smart rounding to prevent micro-adjustments and potential stuttering.
- P-state aware: applies calculated offsets primarily to P0 state, using stable defaults for others.
- Integrated memory clock offset control.

**Usage:**
```bash
sudo python3 gpu_offset_control_v2
```

### 2. `nvidia_stats.c` (C)
A utility to read advanced NVIDIA GPU statistics not typically available via `nvidia-smi`, such as Core Voltage, Hotspot Temperature, and Memory Temperature. It utilizes undocumented NVAPI calls.

**Compilation:**
```bash
gcc -o nvidia_stats nvidia_stats.c -ldl
```

**Usage:**
```bash
./nvidia_stats
```

### 3. Shell Scripts (Legacy)
- `nvidia-offset-advanced.sh`: Advanced shell script for frequency and memory offset control using `nvidia-settings`.
- `nvidia_offset_basic.sh`: A simplified version for basic frequency offset control.

**Usage:**
```bash
./nvidia-offset-advanced.sh
```

## Requirements
- **NVIDIA Driver:** 555+ (Recommended)
- **Python:** 3.12+
- **Python Packages:** `nvidia-ml-py` (required for `gpu_offset_control_v2`)
- **System Utilities:**
  - `nvidia-smi` (v565 or earlier is required for voltage reading via smi)
  - `nvidia-settings` (required for shell scripts)
- **Compiler:** `gcc` (required to compile `nvidia_stats.c`)

## Settings Explanation
The scripts use various parameters to calculate the optimal offset. Below is an explanation of the core settings:

- **nvidia_smi_lgc_min & nvidia_smi_lgc_max**: Sets the frequency range within which the GPU is allowed to operate (locked graphics clocks). *Note: Supported on RTX 30xx (Ampere) and newer.*
- **frequency_min & frequency_max**: The frequency range used for linear offset interpolation.
- **freq_offset_min & freq_offset_max**: The base frequency offset range applied between `frequency_min` and `frequency_max`.
- **temperature_min & temperature_max**: The temperature range used for calculating additional "drain" offsets.
- **plimit_min & plimit_max**: GPU power consumption range for power-based offset calculations.
- **low_freq_min, low_freq_max / high_freq_min, high_freq_max**: Defines frequency regions where specific "drain" offsets are applied.
- **drain_offset_lmin, drain_offset_lmax / drain_offset_hmin, drain_offset_hmax**: Offsets that account for the positive/negative temperature coefficients of transistors in different frequency/voltage regions.
- **power_offset_min, power_offset_max**: Additional offset that further downvolts the GPU core when it is not fully loaded.
- **critical_temp_min & critical_temp_max**: Defines a temperature range where certain offset calculations (like drain offset) are disabled or fixed to prevent instability due to voltage fluctuations.
