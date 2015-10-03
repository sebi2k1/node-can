{
  "targets": [
    {
      "target_name": "can",
      "sources": [ "src/rawchannel.cc" ],
	   "include_dirs": [
        "<!(node -e \"require('nan')\")"
      ]
    },
    {
      "target_name": "can_signals",
      "sources": [ "src/signals.cc" ],
	   "include_dirs": [
        "<!(node -e \"require('nan')\")"
      ]
    }
  ]
}
