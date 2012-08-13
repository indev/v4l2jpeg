#include <stdlib.h>
#include <string.h>

#include <v8.h>
#include <node.h>
#include <node_buffer.h>

#include "v4l2jpeg.h"
#include "mjpegtojpeg.h"

using namespace v8;
using namespace node;

v8::Local<v8::Object> caller_this;

void jpeg_callback( const void *data, int size )
{
	Buffer *buf = Buffer::New(size);
    memcpy(Buffer::Data(buf), data, size);

	Handle<Value> argv[3] = {
		String::New("jpeg"), // event name
		Local<Value>::New(buf->handle_),
		Integer::New(size)  // argument
	};

	MakeCallback(caller_this, "emit", 3, argv);

}

struct JpegEmitter: ObjectWrap {
	static Handle<Value> New(const Arguments& args);
	static Handle<Value> Ping(const Arguments& args);
	static Handle<Value> Connect(const Arguments& args);
};


Handle<Value> JpegEmitter::New(const Arguments& args) {
	HandleScope scope;

	assert(args.IsConstructCall());
	JpegEmitter* self = new JpegEmitter();
	self->Wrap(args.This());

	return scope.Close(args.This());
}

Handle<Value> JpegEmitter::Ping(const Arguments& args) {
	HandleScope scope;

	Handle<Value> argv[2] = {
		String::New("ping"), // event name
		args[0]->ToString()  // argument
	};

	MakeCallback(args.This(), "emit", 2, argv);

	return Undefined();
}

Handle<Value> JpegEmitter::Connect(const Arguments& args) {

	caller_this = args.This();

	Handle<Value> argv[2] = {
		String::New("ping"), // event name
		String::New("jpeg")  // argument
	};
	MakeCallback(args.This(), "emit", 2, argv);

	internal_main( &jpeg_callback );

	Handle<Value> argv2[2] = {
		String::New("ping"), // event name
		String::New("run")  // argument
	};
	MakeCallback(args.This(), "emit", 2, argv2);

	return Undefined();
}

extern "C" {
	void init(Handle<Object> target) {
		HandleScope scope;

		Local<FunctionTemplate> t = FunctionTemplate::New(JpegEmitter::New);
		t->InstanceTemplate()->SetInternalFieldCount(1);
		t->SetClassName(String::New("JpegEmitter"));

		NODE_SET_PROTOTYPE_METHOD(t, "ping", JpegEmitter::Ping);
		NODE_SET_PROTOTYPE_METHOD(t, "connect", JpegEmitter::Connect);

		target->Set(String::NewSymbol("JpegEmitter"), t->GetFunction());
	}

	NODE_MODULE(v4l2jpeg, init);
}