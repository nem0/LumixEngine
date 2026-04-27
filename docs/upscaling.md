# Upscaling (DLSS and FSR3)

Lumix supports NVIDIA DLSS and AMD FSR3 as anti-aliasing / upscaling paths.

## Fallback behavior

Startup order is:
1. Try DLSS initialization.
2. If DLSS init fails, try FSR3 initialization.
3. If both DLSS and FSR3 init fail, the engine continues with built-in TAA (no DLSS/FSR3 plugin active).

## DLSS

1. Install an NVIDIA driver that supports DLSS/NGX for your GPU.
2. Copy required NGX runtime files next to your executable (for example `scripts\tmp\vs2022\bin\Debug`):
`nvsdk_ngx_d.dll` (or `nvsdk_ngx.dll`) and `nvngx_dlss.dll`.
3. Run the engine with the DX12 renderer.

The engine tries to initialize DLSS first. If DLSS is unavailable or init fails, startup falls back to FSR3 initialization.

Implementation notes:
- Implemented in [dlss.inl](../src/renderer/dlss.inl).
- DX12-only path.
- Exposes DLAA / Quality / Balanced / Performance / Ultra Performance in debug UI.

## FSR3

1. Download the [FSR3 SDK](https://gpuopen.com/fidelityfx-super-resolution-3/).
2. Copy the `PrebuiltSignedDLL\amd_fidelityfx_dx12.dll` file from the SDK to the directory containing your executable (for example `scripts\tmp\vs2022\bin\Debug`).
3. The engine will automatically detect and use FSR3 when available.

Implementation can be found in [fsr3.inl](../src/renderer/fsr3.inl).
