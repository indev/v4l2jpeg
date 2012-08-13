var JpegEmitter = require(__dirname + '/build/Release/v4l2jpeg.node').JpegEmitter;
var events = require('events');

inherits(JpegEmitter, events.EventEmitter);
exports.JpegEmitter = JpegEmitter;

// extend prototype
function inherits(target, source) {
  for (var k in source.prototype)
    target.prototype[k] = source.prototype[k];
}