# Changelog

## [4.1.0] - 2026-05-17

### Changed
- Migrated native addon from `nan` to `node-addon-api` (N-API). The binary
  is now ABI-stable across Node.js major versions: a module compiled for
  Node.js 20 loads unchanged on Node.js 22 and 24 without recompilation.
  This resolves a common issue where upgrading Node.js silently broke
  downstream packages (e.g. ioBroker adapters) until a manual rebuild was
  performed. The JavaScript API is unchanged.

### Added
- `Dockerfile.build-test` for verifying compilation on Linux without a
  physical CAN interface.

## [4.0.7] - 2024-03-13

### Changed
- Bump brace-expansion from 1.1.11 to 1.1.12.
- Upgrade NAN.

## [4.0.6] - 2022-11-14

### Added
- `err` boolean type to `can.d.ts` Message.
