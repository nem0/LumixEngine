# Crash Reporting

Lumix Engine includes automatic crash reporting to help improve stability. When the engine crashes, it can send anonymized crash data to Sentry for analysis.

## Features
- **Minidump Generation**: Creates `minidump.dmp` in the working directory for debugging.
- **Sentry Integration**: Sends crash reports to Sentry with stack traces and system info.
- **Configurable Flags**: Control what happens on crash via `CrashReportFlags`:
  - `ENABLED`: Enables crash handling.
  - `MESSAGE_BOX`: Shows a message box with crash info.
  - `LOG`: Logs crash info to file.
  - `STDERR`: Outputs to stderr.
  - `SENTRY`: Sends report to Sentry.

## Configuration
- Default: All flags enabled (`ENABLE_ALL`).
- Disable Sentry: Use `-no_crash_report` or configure flags without `SENTRY`.
- Programmatically: Call `Lumix::configureCrashReport(flags)`.

Reports are sent to Lumix Engine's Sentry project for internal use only.