var JpegEmitter = require('./jpegemitter').JpegEmitter;

var e = new JpegEmitter();

e.on('ping', function(s) {
  console.error('You say ping, I say %s.', s);
});

var first = true;

e.on('jpeg', function( buf, size ) {
	console.log(buf);
	console.log(size);

	if ( first ) {
		require("fs").writeFile("files/test.jpg", buf, function(err) {
		  console.log(err);
		});

		first = false;
	}
})

// emits a 'ping' event from c++ land
e.ping('pong');

e.connect();