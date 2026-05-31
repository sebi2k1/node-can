{
  "targets": [
    {
      "target_name": "can",
      "sources": [ "native/can.cc" ],
      "include_dirs": [
        "<!@(node -p \"require('node-addon-api').include_dir\")"
      ],
      "defines": [ "NAPI_DISABLE_CPP_EXCEPTIONS" ],
      "cflags_cc": [ "-std=c++20" ]
    },
    {
      "target_name": "can_signals",
      "sources": [ "native/signals.cc" ],
      "include_dirs": [
        "<!@(node -p \"require('node-addon-api').include_dir\")"
      ],
      "defines": [ "NAPI_DISABLE_CPP_EXCEPTIONS" ],
      "cflags_cc": [ "-std=c++20" ]
    }
  ]
}
