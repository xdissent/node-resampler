#ifndef RESAMPLER_H
#define RESAMPLER_H

#include <stdlib.h>
#include <memory.h>
#include <node.h>
#include <node_buffer.h>
#include <libresample.h>
#include "macros.h"

#define RS_SAMPLE_BYTES 4
#define RS_BUFFER_SAMPLES 1024
#define RS_BUFFER_BYTES 4096
#define RS_BUFFER_PAD 1024

using namespace v8;
using namespace node;

namespace resampler {

class Resampler;

class Resampler : public ObjectWrap {
public:
  static void Init(Handle<Object> exports);

protected:
  Resampler() : ObjectWrap(),
    handle(NULL),
    opened(false),
    resampling(false),
    flushing(false),
    closing(false),
    factor(1),
    quality(1),
    leftovers(NULL),
    leftoversLength(0) {
  }

  ~Resampler() {
    handle = NULL;
    opened = false;
    resampling = false;
    flushing = false;
    closing = false;
    factor = 1;
    quality = 1;
    leftovers = NULL;
    leftoversLength = 0;
  }

  struct Baton {
    uv_work_t request;      // Work request
    Resampler* rs;   // Resampler instance to work on
    Persistent<Function> callback;

    Baton(Resampler* rs_, Handle<Function> cb_) : rs(rs_) {
      rs->Ref();
      request.data = this;
      callback = Persistent<Function>::New(cb_);
    }
    virtual ~Baton() {
      rs->Unref();
      callback.Dispose();
    }
  };

  struct ResampleBaton : Baton {
    Persistent<Object> inBuffer;
    char* inPtr;
    size_t inLength;

    Persistent<Object> outBuffer;
    char* outPtr;
    size_t outLength;

    char* prefix;
    size_t prefixLength;

    ResampleBaton(Resampler* rs_, Handle<Function> cb_, Handle<Object> inBuffer_, char* prefix_, int prefixLength_) :
        Baton(rs_, cb_), inPtr(NULL), inLength(0), outPtr(NULL), outLength(0), prefix(NULL), prefixLength(0) {

      if (!inBuffer_.IsEmpty()) {
        // Persist the input buffer and save pointer
        inBuffer = Persistent<Object>::New(inBuffer_);
        inPtr = Buffer::Data(inBuffer_);
        inLength = Buffer::Length(inBuffer_);
      }

      if (prefixLength_ > 0 && prefix_ != NULL) {
        // Copy prefix
        prefixLength = prefixLength_;
        prefix = (char*)malloc(prefixLength);
        memcpy(prefix, prefix_, prefixLength);
      }

      // Calculate total length of input
      size_t totalLength = inLength + prefixLength;

      if (totalLength > 0) {
        // Create outBuffer based on factor + pad and save pointer
        outLength = totalLength * rs->factor + RS_BUFFER_PAD;
        Buffer* outBuffer_ = Buffer::New(outLength);
        outBuffer = Persistent<Object>::New(outBuffer_->handle_.As<Object>());
        outPtr = Buffer::Data(outBuffer_);
      }
    }
    virtual ~ResampleBaton() {
      if (inPtr != NULL) inBuffer.Dispose();
      if (outPtr != NULL) outBuffer.Dispose();
      if (prefix != NULL) free(prefix);
    }
  };

  struct FlushBaton : Baton {
    Persistent<Object> outBuffer;
    char* outPtr;
    size_t outLength;

    FlushBaton(Resampler* rs_, Handle<Function> cb_) :
        Baton(rs_, cb_), outPtr(NULL), outLength(0) {

      // Create outBuffer and save pointer
      outLength = rs->factor * RS_BUFFER_PAD;
      Buffer* outBuffer_ = Buffer::New(outLength);
      outBuffer = Persistent<Object>::New(outBuffer_->handle_.As<Object>());
      outPtr = Buffer::Data(outBuffer_);
    }
    virtual ~FlushBaton() {
      if (outPtr != NULL) outBuffer.Dispose();
    }
  };

  static Handle<Value> New(const Arguments& args);
  static Handle<Value> Open(const Arguments& args);
  static Handle<Value> Close(const Arguments& args);
  static Handle<Value> Resample(const Arguments& args);
  static Handle<Value> Flush(const Arguments& args);

  static Handle<Value> OpenedGetter(Local<String> str, const AccessorInfo& accessor);

  static void BeginResample(Baton* baton);
  static void DoResample(uv_work_t* req);
  static void AfterResample(uv_work_t* req);

  static void BeginFlush(Baton* baton);
  static void DoFlush(uv_work_t* req);
  static void AfterFlush(uv_work_t* req);

  void* handle;
  bool opened;
  bool resampling;
  bool flushing;
  bool closing;
  double factor;
  int quality;
  char* leftovers;
  int leftoversLength;
};

}

#endif
