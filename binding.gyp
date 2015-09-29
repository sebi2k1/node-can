{
  "targets": [
    {
      "target_name": "can",
      "sources": [ "src/rawchannel.cc" ],
	   "include_dirs": [
        "<!(node -e \"require('nan')\")"
      ]
    }
  ]
}
