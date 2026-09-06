Testing node-can
================

See [CONTRIBUTING.md](CONTRIBUTING.md) for full setup and test instructions.

Quick reference:

```sh
sh prepare_test_env.sh   # one-time: creates vcan0 and vcan1
pnpm test                 # run the full test suite
NODE_CAN_RUN_PRIVILEGED_TESTS=1 pnpm test  # include link lifecycle tests (root or passwordless sudo required)
pnpm exec mocha test/test-signal_conversion.js  # run a single file
```
