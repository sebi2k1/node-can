Testing node-can
================

See [CONTRIBUTING.md](CONTRIBUTING.md) for full setup and test instructions.

Quick reference:

```sh
sh prepare_test_env.sh   # one-time: creates vcan0 and vcan1
npm test                 # run the full test suite
npx mocha test/test-signal_conversion.js  # run a single file
```

Bitrate validation and failure recovery are covered with `vcan`. A successful
bitrate change must be verified with physical CAN hardware because virtual CAN
interfaces do not implement bus timing:

```sh
sudo node -e 'require("./dist/socketcan").setCanBitrate("can0", 500000)'
ip -details link show can0
```
