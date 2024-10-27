# FSR3

1. Download the [FSR3 SDK](https://gpuopen.com/fidelityfx-super-resolution-3/).
2. Copy the `PrebuiltSignedDLL\amd_fidelityfx_dx12.dll` file from the SDK to the directory containing your executable (e.g., `scripts\tmp\vs2022\bin\Debug`).
3. The engine will automatically detect and utilize FSR3.

Implementation can be found in [fsr3.cpp](../src/renderer/fsr3.cpp).