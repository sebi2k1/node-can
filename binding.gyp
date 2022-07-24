{
  "targets": [
    {
      "target_name": "can",
      "sources": [ "native/can.cc" ],
      "include_dirs": [
        "<!(node -e \"require('nan')\")"
      ]
    },
    {
      "target_name": "can_signals",
      "sources": [ "native/signals.cc" ],
	    "include_dirs": [
        "<!(node -e \"require('nan')\")"
      ]
    }
  ]
}
