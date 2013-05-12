node-resampler
==============

PCM audio sample rate conversion for Node.js


Requirements
------------

You must have [libresample](https://ccrma.stanford.edu/~jos/resample/Available_Software.html) installed.

* Debian/Ubuntu via Apt:

```sh
$ sudo apt-get install libresample1-dev
```

* OS X - Install via [homebrew](http://mxcl.github.io/homebrew/) (not yet in master):

```sh
$ brew install https://raw.github.com/xdissent/homebrew/d4f55ba336d66473e7bf167901a0c575c0c4ae17/Library/Formula/libresample.rb
```


Installation
------------

Install with npm:

```sh
$ npm install resampler
```

or via git:

```sh
$ npm install git+https://github.com/xdissent/node-resampler.git
```


Usage
-----

The `resampler` module exports a `stream.Transform` subclass:

```js
var Resampler = require('resampler');

// Pass the input and output sample rates to the constructor:
var resampler = new Resampler(44100, 22050);

// Optionally choose low quality:
// var resampler = new Resampler(44100, 22050, Resampler.QUALITY_LO);

// Treat it like any other transform stream:
process.stdin.pipe(resampler).pipe(process.stdout);
// $ cat audio.pcm | node resample.js > resampled.pcm
```


Examples
--------

Lofi-ify by downsampling by a ridiculous factor:

```coffeescript
Resampler = require 'resampler'

downer = Resampler.new 44100, 1337
upper = Resampler.new 1337, 44100

process.stdin.pipe(downer).pipe(upper).pipe(process.stdout)
# $ cat audio.pcm | coffee lofi.coffee > lofi.pcm
```


FAQ
---

* Why not [SRC libsamplerate](http://www.mega-nerd.com/SRC/)?

  Because it's GPL.
