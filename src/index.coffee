binding = require '../build/Release/binding'
stream = require 'stream'

class Resampler extends stream.Transform
  constructor: (inputRate, outputRate, quality=Resampler.QUALITY_HI) ->
    stream.Transform.call this
    @resampler = new binding.Resampler inputRate, outputRate, quality

  _resample: (samples, callback) ->
    @resampler.resample samples, (err, resampled) =>
      throw err if err?
      @push resampled
      callback()

  _transform: (chunk, encoding, callback) ->
    return @_resample chunk, callback if @resampler.opened
    @resampler.open (err) =>
      throw err if err?
      @_resample chunk, callback

  _flush: (callback) ->
    return callback() unless @resampler.opened
    @resampler.flush (err, resampled) =>
      throw err if err?
      @push resampled
      @resampler.close (err) ->
        throw err if err?
        callback()

Resampler.QUALITY_HI = 1
Resampler.QUALITY_LO = 0

module.exports = Resampler