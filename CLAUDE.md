# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What this project is

`socketcan` is a Node.js native addon that wraps the Linux SocketCAN kernel interface, published to npm. It lets JavaScript/TypeScript code send and receive CAN bus messages via virtual or physical CAN interfaces (e.g. `vcan0`).

**Linux only.** The native addon uses Linux-specific headers (`linux/can.h`, `sys/socket.h`). Nothing will build or run on macOS/Windows.

## Build commands

```sh
npm install          # builds native addon (node-gyp rebuild) + TypeScript (tsc via prepare)
npm run build:all    # explicit: native + TypeScript
npm run build:ts     # TypeScript only → dist/
npm run configure    # node-gyp configure (rarely needed separately)
npm run lint         # eslint --fix
npm run test         # mocha (requires vcan0/vcan1 to exist)
```

Before running tests, set up virtual CAN interfaces (requires Linux + kernel modules):
```sh
sh prepare_test_env.sh   # creates vcan0 and vcan1
```

Run a single test file:
```sh
npx mocha test/test-signal_conversion.js
```

### Docker build validation (use this on macOS)

```sh
docker build -f Dockerfile.build-test -t node-can-build-test .
```

`Dockerfile.build-test` runs `npm install` inside a `node:22-bookworm-slim` image, which triggers the native build and TypeScript compilation. Use this to validate changes without a Linux machine.

## Architecture

The package has three layers that work together:

### 1. Native addons (`native/`, built to `build/Release/`)

Two separate `.node` binaries compiled via `node-gyp` using **node-addon-api** (N-API):

- **`can.node`** — wraps Linux SocketCAN sockets. Exports `RawChannel`, which opens a socket, spawns a `pthread` for reading, and uses a `uv_async` handle to deliver frames to the JS event loop. The TypeScript declaration is in `src/can.d.ts`.
- **`can_signals.node`** — pure bit-manipulation. Exports `encodeSignal` / `decodeSignal` for packing/unpacking signal values into/from CAN frame byte buffers. Declaration is in `src/can_signals.d.ts`.

### 2. KCD parser (`src/parse_kcd.ts`)

Parses `.kcd` XML files (CAN database format) into a typed in-memory structure: `Network → Bus → Message → Signal`. Signals carry metadata: bit offset/length, endianness, slope/intercept scaling, min/max, labels, mux info.

### 3. High-level JS/TS API (`src/socketcan.ts` → `dist/socketcan.js`)

The public entry point. Wraps the two native addons and the KCD parser:

- `createRawChannel(name)` / `createRawChannelWithOptions(name, opts)` — thin wrappers around `can.RawChannel`
- `Signal` — extends `kcd.Signal` with a live `value`, `onChange`/`onUpdate` listeners, and bounds checking in `update()`
- `Message` — holds a named map of `Signal` objects, notifies `onMessageUpdate` listeners
- `DatabaseService` — binds a `RawChannel` to a parsed bus definition. On incoming frames it calls `can_signals.decodeSignal` for each signal, applies slope/intercept, and calls `signal.update()`. `send(msgName)` does the reverse: encodes all signal values back into a frame buffer and calls `channel.send()`

### Data flow

```
Linux kernel (SocketCAN)
  ↕  (socket read/write)
can.node (pthread + uv_async)
  ↕  (Napi callbacks)
socketcan.ts / DatabaseService
  ↕  (encodeSignal / decodeSignal)
can_signals.node
```

## Key constraints

- Node.js ≥ 22 required (`engines` field enforced)
- Tests import from `../dist/socketcan` — run `npm run build:ts` before running tests if you changed TypeScript source
- The `prepare` script runs `tsc` automatically on `npm install`, so `dist/` is always populated after install
