#include "resampler.h"

using namespace resampler;

void Resampler::Init(Handle<Object> exports) {
  Local<FunctionTemplate> tpl = FunctionTemplate::New(New);
  tpl->InstanceTemplate()->SetInternalFieldCount(1);
  tpl->SetClassName(String::NewSymbol("Resampler"));

  NODE_SET_PROTOTYPE_METHOD(tpl, "open", Open);
  NODE_SET_PROTOTYPE_METHOD(tpl, "close", Close);
  NODE_SET_PROTOTYPE_METHOD(tpl, "resample", Resample);
  NODE_SET_PROTOTYPE_METHOD(tpl, "flush", Flush);

  NODE_SET_GETTER(tpl, "opened", OpenedGetter);

  Persistent<Function> constructor = Persistent<Function>::New(tpl->GetFunction());
  exports->Set(String::NewSymbol("Resampler"), constructor);
}

Handle<Value> Resampler::New(const Arguments& args) {
  HandleScope scope;

  if (!args.IsConstructCall())
    return ThrowException(Exception::TypeError(String::New("Use the new operator")));

  REQUIRE_ARGUMENTS(3);

  Resampler* rs = new Resampler();
  rs->Wrap(args.This());

  rs->factor = args[1]->NumberValue() / args[0]->NumberValue();
  rs->quality = args[2]->Int32Value();

  return args.This();
}

Handle<Value> Resampler::OpenedGetter(Local<String> str, const AccessorInfo& accessor) {
  HandleScope scope;
  Resampler* rs = ObjectWrap::Unwrap<Resampler>(accessor.This());
  return Boolean::New(rs->opened);
}

Handle<Value> Resampler::Open(const Arguments& args) {
  HandleScope scope;

  OPTIONAL_ARGUMENT_FUNCTION(0, callback);

  Resampler* rs = ObjectWrap::Unwrap<Resampler>(args.Holder());

  COND_ERR_CALL(rs->opened, callback, "Already open");
  COND_ERR_CALL(rs->closing, callback, "Still closing");

  rs->handle = resample_open(rs->quality, rs->factor, rs->factor);
  COND_ERR_CALL(rs->handle == 0, callback, "Couldn't open");
  rs->opened = true;

  if (!callback.IsEmpty() && callback->IsFunction())  {
    Local<Value> argv[0] = { };
    TRY_CATCH_CALL(args.Holder(), callback, 0, argv);
  }

  return scope.Close(args.Holder());
}

Handle<Value> Resampler::Resample(const Arguments& args) {
  HandleScope scope;

  REQUIRE_ARGUMENTS(2);
  REQUIRE_ARGUMENT_FUNCTION(1, callback);

  Resampler* rs = ObjectWrap::Unwrap<Resampler>(args.Holder());

  COND_ERR_CALL(!rs->opened, callback, "Not open");
  COND_ERR_CALL(rs->resampling, callback, "Already resampling");
  COND_ERR_CALL(!Buffer::HasInstance(args[0]), callback, "First arg must be a buffer");

  // Initialize leftovers if we haven't yet.
  if (rs->leftovers == NULL) {
    rs->leftovers = (char*)malloc(RS_SAMPLE_BYTES);
    rs->leftoversLength = 0;
  }

  char* chunkPtr = Buffer::Data(args[0]);
  size_t chunkLength = Buffer::Length(args[0]);

  int totalLength = chunkLength + rs->leftoversLength;
  int totalSamples = totalLength / RS_SAMPLE_BYTES;

  ResampleBaton* baton;
  Local<Object> inBuffer; // = Local<Object>::New(Null()); // Buffer::New(0);
  inBuffer.Clear();

  if (totalSamples < 0) {
    // Copy entire chunk into leftovers
    memcpy(rs->leftovers + rs->leftoversLength, chunkPtr, chunkLength);
    rs->leftovers += chunkLength;
    baton = new ResampleBaton(rs, callback, inBuffer, NULL, 0);

  } else {
    size_t newLeftoversLength = totalLength % RS_SAMPLE_BYTES;
    size_t inBufferLength = totalLength - rs->leftoversLength - newLeftoversLength;

    // Slice buffer to correct length
    if (inBufferLength > 0) {
      Local<Function> slice = Local<Function>::Cast(args[0]->ToObject()->Get(String::NewSymbol("slice")));
      Local<Value> sliceArgs[2] = { Integer::New(0), Integer::New(inBufferLength) };
      inBuffer = slice->Call(args[0]->ToObject(), 2, sliceArgs)->ToObject();
    }

    // Create baton and let it memcpy leftovers
    baton = new ResampleBaton(rs, callback, inBuffer, rs->leftovers, rs->leftoversLength);

    // Copy new leftovers
    if (newLeftoversLength > 0) memcpy(rs->leftovers, (chunkPtr + chunkLength) - newLeftoversLength, newLeftoversLength);
    rs->leftoversLength = newLeftoversLength;
  }

  rs->resampling = true;
  BeginResample(baton);

  return scope.Close(args.Holder());
}

void Resampler::BeginResample(Baton* baton) {
  uv_queue_work(uv_default_loop(), &baton->request, DoResample, (uv_after_work_cb)AfterResample);
}

void Resampler::DoResample(uv_work_t* req) {
  ResampleBaton* baton = static_cast<ResampleBaton*>(req->data);
  Resampler* rs = baton->rs;

  // Write prefix
  if (baton->prefix != NULL) {
    size_t inBytes = RS_SAMPLE_BYTES - (baton->prefixLength % RS_SAMPLE_BYTES);
    size_t tmpBufferLength = baton->prefixLength + inBytes;
    char* tmpBuffer = (char*)malloc(tmpBufferLength);
    memcpy(tmpBuffer, baton->prefix, baton->prefixLength);

    if (inBytes > 0) {
      memcpy(tmpBuffer + baton->prefixLength, baton->inPtr, inBytes);
      baton->inPtr += inBytes;
      baton->inLength -= inBytes;
    }

    size_t tmpBufferUsed = 0;
    while (tmpBufferUsed < tmpBufferLength) {
      int samplesUsed;
      int result = resample_process(rs->handle, rs->factor, 
        static_cast<float*>(static_cast<void*>(tmpBuffer + tmpBufferUsed)), 
        ((tmpBufferLength - tmpBufferUsed) / RS_SAMPLE_BYTES), 0, 
        &samplesUsed, 
        static_cast<float*>(static_cast<void*>(baton->outPtr)), 
        (baton->outLength / RS_SAMPLE_BYTES));

      baton->outPtr += result * RS_SAMPLE_BYTES;
      baton->outLength -= result * RS_SAMPLE_BYTES;
      tmpBufferUsed += samplesUsed * RS_SAMPLE_BYTES;
    }
    free(tmpBuffer);
  }

  // Do resample operating on inPtr
  while (baton->inLength > 0) {
    int samplesUsed = 0;
    int result = resample_process(rs->handle, rs->factor, 
      static_cast<float*>(static_cast<void*>(baton->inPtr)), 
      (baton->inLength / RS_SAMPLE_BYTES), 0, 
      &samplesUsed, 
      static_cast<float*>(static_cast<void*>(baton->outPtr)), 
      (baton->outLength / RS_SAMPLE_BYTES));

    baton->inPtr += samplesUsed * RS_SAMPLE_BYTES;
    baton->inLength -= samplesUsed * RS_SAMPLE_BYTES;
    baton->outPtr += result * RS_SAMPLE_BYTES;
    baton->outLength -= result * RS_SAMPLE_BYTES;
  }
}

void Resampler::AfterResample(uv_work_t* req) {
  HandleScope scope;

  ResampleBaton* baton = static_cast<ResampleBaton*>(req->data);
  Resampler* rs = baton->rs;

  Local<Object> outBuffer;

  if (baton->outPtr != NULL) {
    size_t origOutLength = Buffer::Length(baton->outBuffer);
    Local<Function> slice = Local<Function>::Cast(baton->outBuffer->Get(String::NewSymbol("slice")));
    Local<Value> sliceArgs[2] = { Integer::New(0), Integer::New(origOutLength - baton->outLength) };
    outBuffer = slice->Call(baton->outBuffer, 2, sliceArgs)->ToObject();
  } else {
    outBuffer = Local<Object>::New(Buffer::New(0)->handle_.As<Object>());
  }

  Local<Value> argv[2] = { Local<Value>::New(Null()), outBuffer };

  rs->resampling = false;
  TRY_CATCH_CALL(rs->handle_, baton->callback, 2, argv);
  delete baton;
}

Handle<Value> Resampler::Flush(const Arguments& args) {
  HandleScope scope;

  REQUIRE_ARGUMENTS(1);
  REQUIRE_ARGUMENT_FUNCTION(0, callback);

  Resampler* rs = ObjectWrap::Unwrap<Resampler>(args.Holder());

  COND_ERR_CALL(!rs->opened, callback, "Not open");
  COND_ERR_CALL(rs->resampling, callback, "Still resampling");
  COND_ERR_CALL(rs->flushing, callback, "Already flushing");

  rs->flushing = true;
  FlushBaton* baton = new FlushBaton(rs, callback); 
  BeginFlush(baton);

  return scope.Close(args.Holder());
}

void Resampler::BeginFlush(Baton* baton) {
  uv_queue_work(uv_default_loop(), &baton->request, DoFlush, (uv_after_work_cb)AfterFlush);
}

void Resampler::DoFlush(uv_work_t* req) {
  FlushBaton* baton = static_cast<FlushBaton*>(req->data);
  Resampler* rs = baton->rs;

  int samplesUsed = 0;
  int result = resample_process(rs->handle, rs->factor, 
    NULL, 0, 1, 
    &samplesUsed, 
    static_cast<float*>(static_cast<void*>(baton->outPtr)), 
    (baton->outLength / RS_SAMPLE_BYTES));

  baton->outPtr += result * RS_SAMPLE_BYTES;
  baton->outLength -= result * RS_SAMPLE_BYTES;
}

void Resampler::AfterFlush(uv_work_t* req) {
  HandleScope scope;

  FlushBaton* baton = static_cast<FlushBaton*>(req->data);
  Resampler* rs = baton->rs;

  size_t origOutLength = Buffer::Length(baton->outBuffer);
  Local<Function> slice = Local<Function>::Cast(baton->outBuffer->Get(String::NewSymbol("slice")));
  Local<Value> sliceArgs[2] = { Integer::New(0), Integer::New(origOutLength - baton->outLength) };
  Local<Object> outBuffer = slice->Call(baton->outBuffer, 2, sliceArgs)->ToObject();

  Local<Value> argv[2] = { Local<Value>::New(Null()), outBuffer };

  rs->flushing = false;
  TRY_CATCH_CALL(rs->handle_, baton->callback, 2, argv);
  delete baton;
}

Handle<Value> Resampler::Close(const Arguments& args) {
  HandleScope scope;

  OPTIONAL_ARGUMENT_FUNCTION(0, callback);

  Resampler* rs = ObjectWrap::Unwrap<Resampler>(args.Holder());

  COND_ERR_CALL(!rs->opened, callback, "Not open");
  COND_ERR_CALL(rs->resampling, callback, "Still resampling");
  COND_ERR_CALL(rs->flushing, callback, "Still flushing");
  COND_ERR_CALL(rs->closing, callback, "Still closing");

  rs->closing = true;
  resample_close(rs->handle);
  rs->opened = false;
  rs->closing = false;

  if (!callback.IsEmpty() && callback->IsFunction())  {
    Local<Value> argv[0] = { };
    TRY_CATCH_CALL(args.Holder(), callback, 0, argv);
  }

  return scope.Close(args.Holder());
}