/* Copyright Sebastian Haas <sebastian@sebastianhaas.info>. All rights reserved.
 * Updated for NodeJS 4.X using NAN by Daniel Gross <dgross@intronik.de>
 * CANFD support by Tournabien Guillaume (@guillaumetournabien)
 * Migrated to node-addon-api (N-API) for ABI stability
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to
 * deal in the Software without restriction, including without limitation the
 * rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
 * sell copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */

#include <napi.h>
#include <uv.h>

#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <assert.h>

#include <pthread.h>

#include <sys/types.h>
#include <sys/time.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <sys/poll.h>
#include <net/if.h>

#include <linux/can.h>
#include <linux/can/raw.h>
#include <linux/sockios.h>

#include <vector>
#include <string>

#define CHECK_CONDITION(expr, str) \
  if (!(expr)) { \
    Napi::Error::New(info.Env(), str).ThrowAsJavaScriptException(); \
    return info.Env().Undefined(); \
  }

#define MAX_FRAMES_PER_ASYNC_EVENT 100

#define likely(x)   __builtin_expect( x , 1)
#define unlikely(x) __builtin_expect( x , 0)

/**
 * Basic CAN & CAN_FD access
 * @module CAN
 */

//-----------------------------------------------------------------------------------------
/**
 * A Raw channel to access a certain CAN channel (e.g. vcan0) via CAN messages.
 * @class RawChannel
 */
class RawChannel : public Napi::ObjectWrap<RawChannel>
{
public:
  static Napi::Object Init(Napi::Env env, Napi::Object exports)
  {
    Napi::Function func = DefineClass(env, "RawChannel", {
      InstanceMethod("addListener",     &RawChannel::AddListener),
      InstanceMethod("start",           &RawChannel::Start),
      InstanceMethod("stop",            &RawChannel::Stop),
      InstanceMethod("send",            &RawChannel::Send),
      InstanceMethod("sendFD",          &RawChannel::SendFD),
      InstanceMethod("setRxFilters",    &RawChannel::SetRxFilters),
      InstanceMethod("setErrorFilters", &RawChannel::SetErrorFilters),
      InstanceMethod("disableLoopback", &RawChannel::DisableLoopback),
    });

    exports.Set("RawChannel", func);
    return exports;
  }

  /**
   * Create a new CAN channel object
   * @constructor RawChannel
   * @param interface {string} interface name to create channel on (e.g. can0)
   * @return new RawChannel object
   */
  explicit RawChannel(const Napi::CallbackInfo& info)
    : Napi::ObjectWrap<RawChannel>(info),
      m_Thread(0), m_Name(""), m_ReadPending(false), m_SocketFd(-1),
      m_ThreadStopRequested(false), m_TimestampsSupported(false),
      m_NonBlockingSend(false), m_napi_env(nullptr), m_async_ctx(nullptr),
      m_StoppedAlready(false), m_SyncInitialized(false)
  {
    Napi::Env env = info.Env();

    if (!info.IsConstructCall()) {
      Napi::Error::New(env, "Must be called with new").ThrowAsJavaScriptException();
      return;
    }
    if (info.Length() < 1) {
      Napi::Error::New(env, "Too few arguments").ThrowAsJavaScriptException();
      return;
    }
    if (!info[0].IsString()) {
      Napi::Error::New(env, "First argument must be a string").ThrowAsJavaScriptException();
      return;
    }

    m_napi_env = env;

    std::string name = info[0].As<Napi::String>().Utf8Value();
    m_Name = name;

    bool timestamps     = false;
    int  protocol       = CAN_RAW;
    bool non_block_send = false;

    if (info.Length() >= 2 && info[1].IsBoolean())
      timestamps = info[1].As<Napi::Boolean>().Value();

    if (info.Length() >= 3 && info[2].IsNumber())
      protocol = info[2].As<Napi::Number>().Int32Value();

    if (info.Length() >= 4 && info[3].IsBoolean())
      non_block_send = info[3].As<Napi::Boolean>().Value();

    m_TimestampsSupported = timestamps;
    m_NonBlockingSend     = non_block_send;

    const int canfd_on = 1;
    m_SocketFd = socket(PF_CAN, SOCK_RAW, protocol);

    if (m_SocketFd > 0)
    {
      can_err_mask_t err_mask;
      struct ifreq ifr;

      memset(&ifr, 0, sizeof(ifr));
      strncpy(ifr.ifr_name, name.c_str(), IFNAMSIZ - 1);
      if (ioctl(m_SocketFd, SIOCGIFINDEX, &ifr) != 0)
        goto on_error;

      err_mask = CAN_ERR_MASK;

      setsockopt(m_SocketFd, SOL_CAN_RAW, CAN_RAW_FD_FRAMES, &canfd_on, sizeof(canfd_on));

      if (setsockopt(m_SocketFd, SOL_CAN_RAW, CAN_RAW_ERR_FILTER, &err_mask, sizeof(err_mask)) != 0)
        goto on_error;

      memset(&m_SocketAddr, 0, sizeof(m_SocketAddr));
      m_SocketAddr.can_family  = PF_CAN;
      m_SocketAddr.can_ifindex = ifr.ifr_ifindex;

      if (bind(m_SocketFd, (struct sockaddr *)&m_SocketAddr, sizeof(m_SocketAddr)) < 0)
        goto on_error;

      pthread_mutex_init(&m_ReadPendingMtx, NULL);
      pthread_cond_init(&m_ReadPendingCond, NULL);
      m_SyncInitialized = true;

      return;

      on_error:
      close(m_SocketFd);
      m_SocketFd = -1;
    }

    if (!IsValid()) {
      Napi::Error::New(env, "Error while creating channel").ThrowAsJavaScriptException();
    }
  }

  ~RawChannel()
  {
    // Stop the reader thread before closing the socket: the thread may be
    // mid-poll() on m_SocketFd, and closing a fd while another thread is
    // polling it is undefined under POSIX (the fd can be reused by an
    // unrelated open() before poll() returns).
    if (m_Thread)
      stopThread();

    if (m_SocketFd >= 0)
      close(m_SocketFd);

    if (m_SyncInitialized) {
      pthread_cond_destroy(&m_ReadPendingCond);
      pthread_mutex_destroy(&m_ReadPendingMtx);
    }

    for (size_t i = 0; i < m_OnMessageListeners.size(); i++)
      delete m_OnMessageListeners.at(i);
    m_OnMessageListeners.clear();

    for (size_t i = 0; i < m_OnChannelStoppedListeners.size(); i++)
      delete m_OnChannelStoppedListeners.at(i);
    m_OnChannelStoppedListeners.clear();
  }

private:
  /**
   * Add listener to receive certain notifications
   * @method addListener
   * @param event {string} onMessage to register for incoming messages
   * @param callback {any} JS callback object
   * @param instance {any} Optional instance pointer to call callback
   */
  Napi::Value AddListener(const Napi::CallbackInfo& info)
  {
    Napi::Env env = info.Env();
    CHECK_CONDITION(info.Length() >= 2, "Too few arguments");
    CHECK_CONDITION(info[0].IsString(), "First argument must be a string");
    CHECK_CONDITION(info[1].IsFunction(), "Second argument must be a function");

    std::string event = info[0].As<Napi::String>().Utf8Value();

    struct listener *l = new struct listener;
    l->callback = Napi::Persistent(info[1].As<Napi::Function>());

    if (info.Length() >= 3 && info[2].IsObject())
      l->handle = Napi::Persistent(info[2].As<Napi::Object>());

    if (event == "onMessage")
      m_OnMessageListeners.push_back(l);
    else if (event == "onStopped")
      m_OnChannelStoppedListeners.push_back(l);
    else {
      delete l;
      Napi::Error::New(env, "Event not supported").ThrowAsJavaScriptException();
      return env.Undefined();
    }

    return info.This();
  }

  /**
   * Start operation on this CAN channel
   * @method start
   */
  Napi::Value Start(const Napi::CallbackInfo& info)
  {
    CHECK_CONDITION(IsValid(), "Cannot start invalid channel");

    Napi::Env env = info.Env();
    uv_loop_t* loop;
    napi_get_uv_event_loop(env, &loop);

    uv_async_init(loop, &m_AsyncReceiverReady, async_receiver_ready_cb);
    m_AsyncReceiverReady.data = this;

    uv_async_init(loop, &m_AsyncChannelStopped, async_channel_stopped_cb);
    m_AsyncChannelStopped.data = this;

    // Create an async context so napi_make_callback runs microtask checkpoints
    // and fires async hooks after each onMessage callback, matching the behaviour
    // of the old NaN implementation (which used node::MakeCallback internally).
    napi_value resource_name;
    napi_create_string_utf8(env, "socketcan:RawChannel:onMessage", NAPI_AUTO_LENGTH, &resource_name);
    napi_async_init(env, (napi_value)info.This(), resource_name, &m_async_ctx);

    m_ThreadStopRequested = false;
    int rc = pthread_create(&m_Thread, NULL, c_thread_entry, this);

    CHECK_CONDITION(rc == 0, "Error starting dispatch thread");

    Ref();

    return info.This();
  }

  /**
   * Stop any operations on this CAN channel
   * @method stop
   */
  Napi::Value Stop(const Napi::CallbackInfo& info)
  {
    CHECK_CONDITION(m_Thread, "Channel not started");
    async_channel_stopped();
    return info.This();
  }

  /**
   * Send a CAN message immediately.
   *
   * PLEASE NOTE: By default, this function may block if the Tx buffer is not available. Please use
   * createRawChannelWithOptions({non_block_send: false}) to get non-blocking sending activated.
   *
   * @method send
   * @param message {Object} JSON object describing the CAN message, keys are id, length, data {Buffer}, ext or rtr
   */
  Napi::Value Send(const Napi::CallbackInfo& info)
  {
    Napi::Env env = info.Env();
    CHECK_CONDITION(info.Length() >= 1, "Invalid arguments");
    CHECK_CONDITION(info[0].IsObject(), "First argument must be an Object");
    CHECK_CONDITION(IsValid(), "Invalid channel!");

    struct can_frame frame;
    Napi::Object obj = info[0].As<Napi::Object>();

    frame.can_id = obj.Get("id").As<Napi::Number>().Uint32Value();

    if (obj.Get("ext").ToBoolean().Value())
      frame.can_id |= CAN_EFF_FLAG;

    if (obj.Get("rtr").ToBoolean().Value())
      frame.can_id |= CAN_RTR_FLAG;

    Napi::Value dataArg = obj.Get("data");
    CHECK_CONDITION(dataArg.IsBuffer(), "Data field must be a Buffer");

    Napi::Buffer<uint8_t> dataBuf = dataArg.As<Napi::Buffer<uint8_t>>();
    CHECK_CONDITION(dataBuf.ByteLength() <= CAN_MAX_DLEN, "Data buffer exceeds CAN frame size");
    frame.can_dlc = dataBuf.ByteLength();
    memcpy(frame.data, dataBuf.Data(), frame.can_dlc);

    {
      struct timeval now;
      if (gettimeofday(&now, 0) == 0) {
        obj.Set("ts_sec",  Napi::Number::New(env, (int32_t)now.tv_sec));
        obj.Set("ts_usec", Napi::Number::New(env, (int32_t)now.tv_usec));
      }
    }

    int flags = m_NonBlockingSend ? MSG_DONTWAIT : 0;
    int i = send(m_SocketFd, &frame, sizeof(struct can_frame), flags);

    return Napi::Number::New(env, i);
  }

  /**
   * Send a CAN FD message immediately.
   *
   * PLEASE NOTE: By default, this function may block if the Tx buffer is not available. Please use
   * createRawChannelWithOptions({non_block_send: false}) to get non-blocking sending activated.
   *
   * PLEASE NOTE: Might fail if underlying device doesnt support CAN FD. Structure is not yet validated.
   *
   * @method sendFD
   * @param message {Object} JSON object describing the CAN message, keys are id, length, data {Buffer}, ext
   */
  Napi::Value SendFD(const Napi::CallbackInfo& info)
  {
    Napi::Env env = info.Env();
    CHECK_CONDITION(info.Length() >= 1, "Invalid arguments");
    CHECK_CONDITION(info[0].IsObject(), "First argument must be an Object");
    CHECK_CONDITION(IsValid(), "Invalid channel!");

    struct canfd_frame frameFD;
    frameFD.flags = 0;

    Napi::Object obj = info[0].As<Napi::Object>();

    frameFD.can_id = obj.Get("id").As<Napi::Number>().Uint32Value();

    if (obj.Get("ext").ToBoolean().Value())
      frameFD.can_id |= CAN_EFF_FLAG;

    if (obj.Get("fd_brs").ToBoolean().Value())
      frameFD.flags |= CANFD_BRS;

    Napi::Value dataArg = obj.Get("data");
    CHECK_CONDITION(dataArg.IsBuffer(), "Data field must be a Buffer");

    Napi::Buffer<uint8_t> dataBuf = dataArg.As<Napi::Buffer<uint8_t>>();
    frameFD.len = dataBuf.ByteLength();
    memset(frameFD.data, 0, sizeof(frameFD.data));
    memcpy(frameFD.data, dataBuf.Data(), frameFD.len);

    {
      struct timeval now;
      if (gettimeofday(&now, 0) == 0) {
        obj.Set("ts_sec",  Napi::Number::New(env, (int32_t)now.tv_sec));
        obj.Set("ts_usec", Napi::Number::New(env, (int32_t)now.tv_usec));
      }
    }

    // Ensure discrete CAN FD length values 0..8, 12, 16, 20, 24, 32, 48, 64 bytes cf ISO11898-1
    static const unsigned char len2dlc[] = {0, 1, 2, 3, 4, 5, 6, 7, 8,
        12, 12, 12, 12,
        16, 16, 16, 16,
        20, 20, 20, 20,
        24, 24, 24, 24,
        32, 32, 32, 32, 32, 32, 32, 32,
        48, 48, 48, 48, 48, 48, 48, 48, 48, 48, 48, 48, 48, 48, 48, 48,
        64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64};

    if (frameFD.len > 64)
      frameFD.len = 64;

    frameFD.len = len2dlc[frameFD.len];

    int flags = m_NonBlockingSend ? MSG_DONTWAIT : 0;
    int i = send(m_SocketFd, &frameFD, sizeof(struct canfd_frame), flags);

    return Napi::Number::New(env, i);
  }

  /**
   * Set a list of active filters to be applied for incoming messages
   * @method setRxFilters
   * @param filters {Object} single filter or array of filter e.g. { id: 0x1ff, mask: 0x1ff, invert: false}
   */
  Napi::Value SetRxFilters(const Napi::CallbackInfo& info)
  {
    CHECK_CONDITION(info.Length() > 0, "Too few arguments");
    CHECK_CONDITION(info[0].IsArray() || info[0].IsObject(), "Invalid argument");
    CHECK_CONDITION(IsValid(), "Channel not ready");

    struct can_filter *rfilter;
    int numfilter = 0;

    if (info[0].IsArray())
    {
      Napi::Array list = info[0].As<Napi::Array>();
      rfilter = (struct can_filter *)malloc(sizeof(struct can_filter) * list.Length());
      CHECK_CONDITION(rfilter, "Couldn't allocate memory for filter list");

      for (uint32_t idx = 0; idx < list.Length(); idx++)
      {
        if (ObjectToFilter(list.Get(idx).As<Napi::Object>(), &rfilter[numfilter]))
          numfilter++;
      }
    }
    else
    {
      rfilter = (struct can_filter *)malloc(sizeof(struct can_filter));
      CHECK_CONDITION(rfilter, "Couldn't allocate memory for filter list");

      if (ObjectToFilter(info[0].As<Napi::Object>(), &rfilter[numfilter]))
        numfilter++;
    }

    if (numfilter)
      setsockopt(m_SocketFd, SOL_CAN_RAW, CAN_RAW_FILTER, rfilter, numfilter * sizeof(struct can_filter));

    if (rfilter)
      free(rfilter);

    return info.This();
  }

  /**
   * Set a list of active filters to be applied for errors
   * @method setErrorFilters
   * @param errorMask {Uint32} CAN error mask
   */
  Napi::Value SetErrorFilters(const Napi::CallbackInfo& info)
  {
    CHECK_CONDITION(info.Length() > 0, "Too few arguments");
    CHECK_CONDITION(info[0].IsNumber(), "Invalid argument");
    CHECK_CONDITION(IsValid(), "Channel not ready");

    can_err_mask_t err_mask = (can_err_mask_t)info[0].As<Napi::Number>().Uint32Value();
    setsockopt(m_SocketFd, SOL_CAN_RAW, CAN_RAW_ERR_FILTER, &err_mask, sizeof(err_mask));

    return info.This();
  }

  /**
   * Disable loopback of channel. By default it is activated
   * @method disableLoopback
   */
  Napi::Value DisableLoopback(const Napi::CallbackInfo& info)
  {
    CHECK_CONDITION(IsValid(), "Channel not ready");
    const int loopback = 0;
    setsockopt(m_SocketFd, SOL_CAN_RAW, CAN_RAW_LOOPBACK, &loopback, sizeof(loopback));
    return info.This();
  }

  void stopThread()
  {
    if (m_Thread)
    {
      pthread_mutex_lock(&m_ReadPendingMtx);
      m_ReadPending         = false;
      m_ThreadStopRequested = true;
      pthread_cond_signal(&m_ReadPendingCond);
      pthread_mutex_unlock(&m_ReadPendingMtx);

      pthread_join(m_Thread, NULL);
      m_Thread = 0;
    }
  }

  uv_async_t m_AsyncReceiverReady;
  uv_async_t m_AsyncChannelStopped;

  struct listener {
    Napi::FunctionReference callback;
    Napi::ObjectReference   handle;
  };

  std::vector<struct listener *> m_OnMessageListeners;
  std::vector<struct listener *> m_OnChannelStoppedListeners;

  pthread_t m_Thread;
  std::string m_Name;

  pthread_mutex_t m_ReadPendingMtx;
  pthread_cond_t  m_ReadPendingCond;
  bool            m_ReadPending;

  int m_SocketFd;
  struct sockaddr_can m_SocketAddr;

  bool m_ThreadStopRequested;
  bool m_TimestampsSupported;
  bool m_NonBlockingSend;

  // Stored for use in uv_async callbacks (always invoked on the main thread)
  napi_env m_napi_env;
  napi_async_context m_async_ctx;

  // Single-shot guard so that JS Stop() and a reader-thread POLLHUP-driven
  // uv_async_send can both call async_channel_stopped() without us running
  // the listener loop or Unref() twice.
  bool m_StoppedAlready;

  // Whether m_ReadPendingMtx / m_ReadPendingCond were successfully
  // initialized. Controls whether the destructor pairs them with *_destroy.
  bool m_SyncInitialized;

  static void * c_thread_entry(void *_this) { assert(_this); reinterpret_cast<RawChannel *>(_this)->ThreadEntry(); return NULL; }

  void ThreadEntry()
  {
    struct pollfd pfd;

    pfd.fd     = m_SocketFd;
    pfd.events = POLLIN|POLLHUP|POLLERR;

    while (!m_ThreadStopRequested)
    {
      pfd.revents = 0;

      pthread_mutex_lock(&m_ReadPendingMtx);

      while (unlikely(m_ReadPending && !m_ThreadStopRequested))
        pthread_cond_wait(&m_ReadPendingCond, &m_ReadPendingMtx);

      pthread_mutex_unlock(&m_ReadPendingMtx);

      if (likely(poll(&pfd, 1, 100) >= 0))
      {
        if (likely(pfd.revents & POLLIN))
        {
          pthread_mutex_lock(&m_ReadPendingMtx);
          uv_async_send(&m_AsyncReceiverReady);
          m_ReadPending = true;
          pthread_mutex_unlock(&m_ReadPendingMtx);
        }

        if (pfd.revents & (POLLHUP|POLLERR))
        {
          uv_async_send(&m_AsyncChannelStopped);
          break;
        }
      }
      else
      {
        break;
      }
    }
  }

  bool IsValid() { return m_SocketFd >= 0; }

  static bool ObjectToFilter(Napi::Object object, struct can_filter *rfilter)
  {
    Napi::Value id   = object.Get("id");
    Napi::Value mask = object.Get("mask");

    if (!id.IsNumber() || !mask.IsNumber())
      return false;

    rfilter->can_id   = id.As<Napi::Number>().Uint32Value();
    rfilter->can_mask = mask.As<Napi::Number>().Uint32Value();

    if (object.Get("invert").ToBoolean().Value())
      rfilter->can_id |= CAN_INV_FILTER;

    rfilter->can_mask &= ~CAN_ERR_FLAG;

    return true;
  }

  static void async_receiver_ready_cb(uv_async_t* handle)
  {
    assert(handle && handle->data);
    reinterpret_cast<RawChannel*>(handle->data)->async_receiver_ready();
  }

  static void async_channel_stopped_cb(uv_async_t* handle)
  {
    assert(handle && handle->data);
    reinterpret_cast<RawChannel*>(handle->data)->async_channel_stopped();
  }

  void async_channel_stopped()
  {
    // Single-shot: JS Stop() and the reader-thread POLLHUP/POLLERR path can
    // both end up calling this. Without the guard the second invocation
    // would re-run the listener loop, double-close the uv handles, and
    // Unref() the strong reference one too many times.
    if (m_StoppedAlready) return;
    m_StoppedAlready = true;

    Napi::Env env(m_napi_env);
    Napi::HandleScope scope(env);

    for (size_t i = 0; i < m_OnChannelStoppedListeners.size(); i++)
    {
      struct listener *l = m_OnChannelStoppedListeners.at(i);
      Napi::Function fn = l->callback.Value();

      if (l->handle.IsEmpty())
        fn.Call(env.Global(), {});
      else
        fn.Call(l->handle.Value(), {});

      if (env.IsExceptionPending()) {
        napi_value exception;
        napi_get_and_clear_last_exception(env, &exception);
        napi_fatal_exception(env, exception);
        break;
      }
    }

    if (m_Thread)
    {
      stopThread();
      uv_close((uv_handle_t *)&m_AsyncReceiverReady, NULL);
      uv_close((uv_handle_t *)&m_AsyncChannelStopped, NULL);
    }

    if (m_async_ctx) {
      napi_async_destroy(m_napi_env, m_async_ctx);
      m_async_ctx = nullptr;
    }

    Unref();
  }

  void async_receiver_ready()
  {
    if (!m_async_ctx) return;

    Napi::Env env(m_napi_env);
    Napi::HandleScope scope(env);

    struct canfd_frame frame;
    unsigned int framesProcessed = 0;

    while (recv(m_SocketFd, &frame, sizeof(struct canfd_frame), MSG_DONTWAIT) > 0)
    {
      Napi::Object obj = Napi::Object::New(env);

      canid_t id    = frame.can_id;
      bool isEff    = frame.can_id & CAN_EFF_FLAG;
      bool isRtr    = frame.can_id & CAN_RTR_FLAG;
      bool isErr    = frame.can_id & CAN_ERR_FLAG;

      id = isEff ? frame.can_id & CAN_EFF_MASK : frame.can_id & CAN_SFF_MASK;

      if (m_TimestampsSupported)
      {
        struct timeval tv;
        if (likely(ioctl(m_SocketFd, SIOCGSTAMP, &tv) >= 0))
        {
          obj.Set("ts_sec",  Napi::Number::New(env, (int32_t)tv.tv_sec));
          obj.Set("ts_usec", Napi::Number::New(env, (int32_t)tv.tv_usec));
        }
      }

      obj.Set("id", Napi::Number::New(env, id));

      if (isEff) obj.Set("ext", Napi::Boolean::New(env, isEff));
      if (isRtr) obj.Set("rtr", Napi::Boolean::New(env, isRtr));
      if (isErr) obj.Set("err", Napi::Boolean::New(env, isErr));

      obj.Set("data", Napi::Buffer<char>::Copy(env, (char *)frame.data, frame.len & 0x7f));

      bool callback_failed = false;
      for (size_t i = 0; i < m_OnMessageListeners.size(); i++)
      {
        struct listener *l = m_OnMessageListeners.at(i);

        // Use napi_make_callback instead of plain fn.Call() so that
        // Node.js runs a microtask checkpoint and fires async hooks
        // after each invocation, matching the old NaN behaviour
        // (Nan::Callback::Call used node::MakeCallback internally).
        napi_value recv_val = l->handle.IsEmpty()
            ? (napi_value)env.Global()
            : (napi_value)l->handle.Value();
        napi_value fn_val   = (napi_value)l->callback.Value();
        napi_value arg      = (napi_value)obj;
        napi_value result;
        napi_make_callback(env, m_async_ctx, recv_val, fn_val, 1, &arg, &result);

        if (env.IsExceptionPending()) {
          napi_value exception;
          napi_get_and_clear_last_exception(env, &exception);
          napi_fatal_exception(env, exception);
          callback_failed = true;
          break;
        }
      }

      if (callback_failed)
        break;

      if (++framesProcessed > MAX_FRAMES_PER_ASYNC_EVENT)
        break;
    }

    pthread_mutex_lock(&m_ReadPendingMtx);
    m_ReadPending = false;
    pthread_cond_signal(&m_ReadPendingCond);
    pthread_mutex_unlock(&m_ReadPendingMtx);
  }
};

//-----------------------------------------------------------------------------------------

static Napi::Object ModuleInit(Napi::Env env, Napi::Object exports)
{
  return RawChannel::Init(env, exports);
}

NODE_API_MODULE(can, ModuleInit)
