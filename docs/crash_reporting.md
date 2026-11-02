# Crash Reporting

Lumix Engine includes automatic crash reporting to help improve stability. When the engine crashes, it can send anonymized crash data to Sentry for analysis.

## Features
- **Minidump Generation**: Creates `minidump.dmp` in the working directory for debugging.
- **Sentry Integration**: Sends crash reports to Sentry with stack traces and system info.
- **Structured Callstack**: On Windows the crash is sent as a Sentry `exception` with a structured stacktrace (up to 64 frames of the crashing thread). Sentry groups issues by this stacktrace. Frames are symbolicated on the crashing machine, so they have function, file and line only if PDBs are present there. Otherwise the function is `module+0xrva`, where the offset is relative to the module base, so it's stable across runs of the same build (but differs between builds). PDBs are not uploaded to Sentry. The full text report, including the profiler stack, is attached as the `crash_report` extra. Without a captured callstack, only the text is sent as a message.
- **Log Attachment**: Sentry attaches the last 128 KiB of the engine's active log as `engine.log`, including `lumix_app.log` for the standalone app.
- **Configurable Flags**: Control what happens on crash via `CrashReportFlags`:
  - `ENABLED`: Enables crash handling.
  - `MESSAGE_BOX`: Shows a message box with crash info.
  - `LOG`: Logs crash info to file.
  - `STDERR`: Outputs to stderr.
  - `SENTRY`: Sends report to Sentry.

## Direct3D 12 GPU diagnostics

Pass `gpu::InitFlags::GPU_VALIDATION` to GPU initialization or launch with `-gpu_validation` to enable GPU-based validation. This also enables the required D3D12 debug layer, independently of DRED. It is disabled by default and can significantly slow rendering. If the debug layer or validation interface is unavailable, the engine logs a warning and continues without GPU-based validation.

Pass `gpu::InitFlags::DRED` to GPU initialization or launch with `-dred` to enable DRED auto-breadcrumbs and GPU page fault reporting before device creation, including in release builds. The engine does not force DRED on by default. This does not require `-debug_gpu`, the debug layer, or GPU-based validation. DRED adds some runtime and memory overhead; unsupported runtimes log a warning and continue.

On the first device removal, the engine writes `DRED:` entries to the normal log: command-list progress, operations near the last completed breadcrumb, and any GPU page fault address with matching existing and recently freed allocation names and types. Breadcrumb operation indices are zero-based; a pending operation is not necessarily the exact cause of the hang. Missing breadcrumbs or page fault data are reported explicitly.

## Configuration
- Default: All flags enabled (`ENABLE_ALL`).
- Disable Sentry: Use `-no_crash_report` or configure flags without `SENTRY`.
- Programmatically: Call `Lumix::configureCrashReport(flags)`.

## User feedback

`sendFeedback(allocator, message, email, attachment_path)` from `core/debug.h` sends standalone feedback to Sentry and blocks until the upload ends (15 s timeout). It is independent of `CrashReportFlags`, since it is always an explicit user action. `email` and `attachment_path` are optional; the attachment can be any file on disk up to 5 MiB.

Evox scripts call `core:sentry` `sendFeedback(message, email, attachment_path) : bool`. Only pass attachment paths the user chose, because the script can upload any readable file. Only the Windows build implements this; elsewhere `sendFeedback` returns false.

Reports are sent to Lumix Engine's Sentry project for internal use only.
