# Contributing

Thanks for your interest in contributing to `socketcan`. This is a small Linux-only native addon project, so a few things are worth knowing before you start.

## Prerequisites

**Linux is required.** The native addon uses Linux-specific headers (`linux/can.h`, `sys/socket.h`). You cannot build or run the native layer on macOS or Windows. If you're on macOS, use the Docker workflow described below.

- Node.js ≥ 22
- `node-gyp` build toolchain (`build-essential`, `python3`)
- For tests: Linux kernel with SocketCAN modules (`vcan`)

## Getting started

```sh
git clone https://github.com/sebi2k1/node-can.git
cd node-can
npm install        # builds native addon + compiles TypeScript
```

## Building

```sh
npm run build:all  # native addon + TypeScript
npm run build:ts   # TypeScript only → dist/
npm run lint       # ESLint with auto-fix
```

### On macOS (Docker)

```sh
docker build -f Dockerfile.build-test -t node-can-build-test .
```

This runs `npm install` inside a `node:22-bookworm-slim` container and validates the native build and TypeScript compilation.

## Running tests

Tests require two virtual CAN interfaces. Set them up once per session:

```sh
sh prepare_test_env.sh   # creates vcan0 and vcan1 (requires root / modprobe)
npm test
```

Run a single test file:

```sh
npx mocha test/test-signal_conversion.js
```

Tests import from `../dist/socketcan`, so always run `npm run build:ts` after changing TypeScript source before running tests.

## What to work on

Check the [GitHub Issues](https://github.com/sebi2k1/node-can/issues) for open bugs and feature requests. If you want to work on something not listed, open an issue first to discuss it — this avoids duplicated effort or work that doesn't fit the project direction.

## Submitting changes

1. Fork the repo and create a branch from `master`.
2. Make your changes. If you touch native code, validate with the Docker build.
3. Add or update tests for any changed behavior.
4. Run `npm run lint` and `npm test` — both must pass.
5. Open a pull request against `master` with a clear description of what changed and why.

Keep pull requests focused. One logical change per PR is easier to review and merge.

## Code style

TypeScript source is formatted with Prettier and linted with ESLint. Run `npm run lint` to auto-fix. Native C++ code (`native/`) follows the style of the surrounding code — no tabs, 4-space indent.

## Reporting bugs

Open a GitHub Issue with:
- Node.js version (`node --version`)
- CAN interface type (virtual `vcan` or physical hardware)
- Minimal reproduction — a short script or test case
- What you expected vs. what happened

## License

By contributing you agree that your changes will be licensed under the project's [MIT License](LICENSE).
