/* Copyright Sebastian Haas <sebastian@sebastianhaas.info>. All rights reserved.
 * Updated for NodeJS 4.X using NAN by Daniel Gross <dgross@intronik.de>
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

#include <nan.h>
#include <node_buffer.h>

#include <stdlib.h>
#include <unistd.h>
#include <string.h>

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

using namespace v8;

#define CHECK_CONDITION(expr, str) if(!(expr)) return Nan::ThrowError(str);

#define MAX_FRAMES_PER_ASYNC_EVENT 100

#define likely(x)   __builtin_expect( x , 1)
#define unlikely(x) __builtin_expect( x , 0)

// symbols
#define SYMBOL(aString) Nan::New((aString)).ToLocalChecked()
#define tssec_symbol    SYMBOL("ts_sec")
#define tsusec_symbol   SYMBOL("ts_usec")
#define id_symbol       SYMBOL("id")
#define mask_symbol     SYMBOL("mask")
#define invert_symbol   SYMBOL("invert")
#define ext_symbol      SYMBOL("ext")
#define rtr_symbol      SYMBOL("rtr")
#define err_symbol      SYMBOL("err")
#define data_symbol     SYMBOL("data")
#define canfd_symbol    SYMBOL("canfd")        

/**
 * Basic CAN & CAN_FD access
 * @module CAN
 */

/* TODO */
// define the variable as Member of this instance and not as Global Variable
// Need help to declare properly this variable
int Flag_CANFD_Used=0;         // Add a global Flag to treat the information according the interface capability, see Init function

//-----------------------------------------------------------------------------------------
/**
 * A Raw channel to access a certain CAN channel (e.g. vcan0) via CAN messages.
 * @class RawChannel
 */
class RawChannel : public Nan::ObjectWrap
{
private:
  static Nan::Persistent<v8::Function> constructor;

public:
  static NAN_MODULE_INIT(Init)
  {
    Nan::HandleScope scope;

    // Prepare constructor template
    v8::Local<v8::FunctionTemplate> tpl = Nan::New<v8::FunctionTemplate>(New);
    tpl->SetClassName(Nan::New("Channel").ToLocalChecked());  // FIXME: why is here Channel instead RawChannel used
    tpl->InstanceTemplate()->SetInternalFieldCount(1);        // for storing (this)

    // Prototype
    Nan::SetPrototypeMethod(tpl, "addListener",     AddListener);
    Nan::SetPrototypeMethod(tpl, "start",           Start);
    Nan::SetPrototypeMethod(tpl, "stop",            Stop);
    Nan::SetPrototypeMethod(tpl, "send",            Send);
    Nan::SetPrototypeMethod(tpl, "sendFD",          SendFD);                  //add dedicate function for SEND_CANFD frame
    Nan::SetPrototypeMethod(tpl, "setRxFilters",    SetRxFilters);
    Nan::SetPrototypeMethod(tpl, "setErrorFilters", SetErrorFilters);
    Nan::SetPrototypeMethod(tpl, "disableLoopback", DisableLoopback);

    // constructor
    constructor.Reset(Nan::GetFunction(tpl).ToLocalChecked());
    Nan::Set(target, Nan::New("RawChannel").ToLocalChecked(), Nan::GetFunction(tpl).ToLocalChecked());
  }

private:
  explicit RawChannel(const char *name, bool timestamps, int protocol, bool non_block_send)
    : m_Thread(0), m_Name(name), m_SocketFd(-1)
  {
    static const int canfd_on = 1;
    m_SocketFd = socket(PF_CAN, SOCK_RAW, protocol);    // OPen the socket
    m_ThreadStopRequested = false;
    m_TimestampsSupported = timestamps;
    m_NonBlockingSend = non_block_send;

    if (m_SocketFd > 0)
    {
      can_err_mask_t err_mask;
      struct ifreq ifr;

      memset(&ifr, 0, sizeof(ifr));
      strncpy(ifr.ifr_name, name, IFNAMSIZ - 1);
      if (ioctl(m_SocketFd, SIOCGIFINDEX, &ifr) != 0)
        goto on_error;

      // Configuration updated to use the CAN_FD 
      err_mask = CAN_ERR_MASK;
      Flag_CANFD_Used = 1 ;                                                                               // try to use CAN_FD first
      if (setsockopt(m_SocketFd, SOL_CAN_RAW, CAN_RAW_FD_FRAMES,&canfd_on, sizeof(canfd_on)) != 0)        // configuration for CAN_FD
      {
        Flag_CANFD_Used = 0 ;                                                                               // CAN_FD not usable
        if (setsockopt(m_SocketFd, SOL_CAN_RAW, CAN_RAW_ERR_FILTER, &err_mask, sizeof(err_mask)) != 0)      // So use the configuration for CAN_HS
          {
            goto on_error;
          }
      }


      memset(&m_SocketAddr, 0, sizeof(m_SocketAddr));
      m_SocketAddr.can_family = PF_CAN;
      m_SocketAddr.can_ifindex = ifr.ifr_ifindex;

      if (bind(m_SocketFd, (struct sockaddr *)&m_SocketAddr, sizeof(m_SocketAddr)) < 0)
        goto on_error;

      pthread_mutex_init(&m_ReadPendingMtx, NULL);
      pthread_cond_init(&m_ReadPendingCond, NULL);

      m_ReadPending = false;

      return;

    on_error:
      close(m_SocketFd);
      m_SocketFd = -1;
    }
  }

  ~RawChannel()
  {
    for (size_t i = 0; i < m_OnMessageListeners.size(); i++)
      delete m_OnMessageListeners.at(i);

    m_OnMessageListeners.clear();

    for (size_t i = 0; i < m_OnChannelStoppedListeners.size(); i++)
      delete m_OnChannelStoppedListeners.at(i);

    m_OnChannelStoppedListeners.clear();

    if (m_SocketFd >= 0)
      close(m_SocketFd);

    if (m_Thread)
      stopThread();
  }

  /**
   * Create a new CAN channel object
   * @constructor RawChannel
   * @param interface {string} interface name to create channel on (e.g. can0)
   * @return new RawChannel object
   */
  static NAN_METHOD(New)
  {
    bool timestamps     = false;
    int protocol        = CAN_RAW;
    bool non_block_send = false;

    CHECK_CONDITION(info.IsConstructCall(), "Must be called with new");
    CHECK_CONDITION(info.Length() >= 1, "Too few arguments");
    CHECK_CONDITION(info[0]->IsString(), "First argument must be a string");

    Nan::Utf8String ascii( Nan::To<String>(info[0]).ToLocalChecked() );

    if (info.Length() >= 2)
    {
      if (info[1]->IsBoolean())
        timestamps = info[1]->IsTrue();
    }

    if (info.Length() >= 3)
    {
      if (info[2]->IsInt32())
        protocol = info[2]->IntegerValue(Nan::GetCurrentContext()).FromJust();
    }

    if (info.Length() >= 4)
    {
      if (info[3]->IsBoolean())
        non_block_send = info[3]->IsTrue();
    }

    RawChannel* hw = new RawChannel(*ascii, timestamps, protocol, non_block_send);
    hw->Wrap(info.This());

    CHECK_CONDITION(hw->IsValid(), "Error while creating channel");

    info.GetReturnValue().Set(info.This());
  }

  /**
   * Add listener to receive certain notifications
   * @method addListener
   * @param event {string} onMessage to register for incoming messages
   * @param callback {any} JS callback object
   * @param instance {any} Optional instance pointer to call callback
   */
  static NAN_METHOD(AddListener)
  {
    RawChannel* hw = Nan::ObjectWrap::Unwrap<RawChannel>(info.This());
    CHECK_CONDITION(info.Length() >= 2, "Too few arguments");
    CHECK_CONDITION(info[0]->IsString(), "First argument must be a string");
    CHECK_CONDITION(info[1]->IsFunction(), "Second argument must be a function");

    Nan::Utf8String event_utf8(Nan::To<String>(info[0]).ToLocalChecked());
    std::string event = *event_utf8;

    struct listener *listener = new struct listener;
    listener->callback.Reset(info[1].As<v8::Function>());

    if (info.Length() >= 3 && info[2]->IsObject())
        listener->handle.Reset(Nan::To<Object>(info[2]).ToLocalChecked());

    if (event.compare("onMessage") == 0)
      hw->m_OnMessageListeners.push_back(listener);
    else if (event.compare("onStopped") == 0)
      hw->m_OnChannelStoppedListeners.push_back(listener);
    else
      goto on_error;

    info.GetReturnValue().Set(info.This());

    return;

on_error:
    delete listener;

    return Nan::ThrowError("Event not supported");
  }

  /**
   * Start operation on this CAN channel
   * @method start
   */
  static NAN_METHOD(Start)
  {
    RawChannel* hw = ObjectWrap::Unwrap<RawChannel>(info.Holder());

    CHECK_CONDITION(hw->IsValid(), "Cannot start invalid channel");

    // FIXME: why is an cast on the third argument required
    // see: https://github.com/nodejs/nan/issues/151
    uv_async_init(uv_default_loop(), &hw->m_AsyncReceiverReady, (uv_async_cb) async_receiver_ready_cb);
    hw->m_AsyncReceiverReady.data = hw;

    uv_async_init(uv_default_loop(), &hw->m_AsyncChannelStopped, (uv_async_cb) async_channel_stopped_cb);
    hw->m_AsyncChannelStopped.data = hw;

    hw->m_ThreadStopRequested = false;
    pthread_create(&hw->m_Thread, NULL, c_thread_entry, hw);

    CHECK_CONDITION(hw->m_Thread, "Error starting dispatch thread");

    hw->Ref();

    info.GetReturnValue().Set(info.This());
  }

  /**
   * Stop any operations on this CAN channel
   * @method stop
   */
  static NAN_METHOD(Stop)
  {
    RawChannel* hw = ObjectWrap::Unwrap<RawChannel>(info.Holder());

    CHECK_CONDITION(hw->m_Thread, "Channel not started");

    hw->async_channel_stopped();

    info.GetReturnValue().Set(info.This());
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
  static NAN_METHOD(Send)
  {
    RawChannel* hw = ObjectWrap::Unwrap<RawChannel>(info.Holder());

    CHECK_CONDITION(info.Length() >= 1, "Invalid arguments");
    CHECK_CONDITION(info[0]->IsObject(), "First argument must be an Object");
    CHECK_CONDITION(hw->IsValid(), "Invalid channel!");
    struct can_frame frame;
    v8::Local<v8::Context> context = info.GetIsolate()->GetCurrentContext();
    v8::Local<v8::Object> obj = Nan::To<Object>(info[0]).ToLocalChecked();

    // CAN HS FRAME
    // ---------------
    // TODO: Check for correct structure of message object

      frame.can_id = obj->Get(context, id_symbol).ToLocalChecked()->ToUint32(context).ToLocalChecked()->Value();
      if (obj->Get(context, ext_symbol).ToLocalChecked()->IsTrue())
        frame.can_id |= CAN_EFF_FLAG;

      if (obj->Get(context, rtr_symbol).ToLocalChecked()->IsTrue())
        frame.can_id |= CAN_RTR_FLAG;

      v8::Local<v8::Value> dataArg = obj->Get(context, data_symbol).ToLocalChecked();

      CHECK_CONDITION(node::Buffer::HasInstance(dataArg), "Data field must be a Buffer");

      // Get frame data
      frame.can_dlc = node::Buffer::Length(Nan::To<Object>(dataArg).ToLocalChecked());    
      memcpy(frame.data, node::Buffer::Data(Nan::To<Object>(dataArg).ToLocalChecked()), frame.can_dlc);

      { // set time stamp when sending data
        struct timeval now;
        if ( 0==gettimeofday(&now, 0)) {
          Nan::Set(obj, tssec_symbol, Nan::New((int32_t)now.tv_sec));
          Nan::Set(obj, tsusec_symbol, Nan::New((int32_t)now.tv_usec));
        }
      }

      int flags = 0;

      if (hw->m_NonBlockingSend)
        flags = MSG_DONTWAIT;
    int i = send(hw->m_SocketFd, &frame, sizeof(struct can_frame), flags);  
    info.GetReturnValue().Set(i);
  }

 /**
   * Send a CAN message immediately.
   *
   * PLEASE NOTE: By default, this function may block if the Tx buffer is not available. Please use
   * createRawChannelWithOptions({non_block_send: false}) to get non-blocking sending activated.
   *
   * Added by Guillaume Tournabien 2021_03_14
   * Note : All the setup is not supported and no protection are included
   * 
   * @method send
   * @param message {Object} JSON object describing the CAN message, keys are id, length, data {Buffer}, ext or rtr
   */
  static NAN_METHOD(SendFD)
  {
    int requiered_mtu = 1;
    RawChannel* hw = ObjectWrap::Unwrap<RawChannel>(info.Holder());

    CHECK_CONDITION(info.Length() >= 1, "Invalid arguments");
    CHECK_CONDITION(info[0]->IsObject(), "First argument must be an Object");
    CHECK_CONDITION(hw->IsValid(), "Invalid channel!");
    struct canfd_frame frameFD;                         
    frameFD.flags = 0;                                  // Flags not used on the function

    v8::Local<v8::Context> context = info.GetIsolate()->GetCurrentContext();
    v8::Local<v8::Object> obj = Nan::To<Object>(info[0]).ToLocalChecked();

    // CAN FD FRAME
    // ---------------
    // TODO: Check for correct structure of message object

      frameFD.can_id = obj->Get(context, id_symbol).ToLocalChecked()->ToUint32(context).ToLocalChecked()->Value();
      if (obj->Get(context, ext_symbol).ToLocalChecked()->IsTrue())
        frameFD.can_id  |= CAN_EFF_FLAG;

      v8::Local<v8::Value> dataArg = obj->Get(context, data_symbol).ToLocalChecked();

      CHECK_CONDITION(node::Buffer::HasInstance(dataArg), "Data field must be a Buffer");

      // Get frame data
      frameFD.len = node::Buffer::Length(Nan::To<Object>(dataArg).ToLocalChecked()); 
      memcpy(frameFD.data, node::Buffer::Data(Nan::To<Object>(dataArg).ToLocalChecked()), frameFD.len);
      
      { // set time stamp when sending data
        struct timeval now;
        if ( 0==gettimeofday(&now, 0)) {
          Nan::Set(obj, tssec_symbol, Nan::New((int32_t)now.tv_sec));
          Nan::Set(obj, tsusec_symbol, Nan::New((int32_t)now.tv_usec));
        }
      }

      int flags = 0;

      if (hw->m_NonBlockingSend)
      flags = MSG_DONTWAIT;
      
      /* ensure discrete CAN FD length values 0..8, 12, 16, 20, 24, 32, 64 */
      // TODO : Need to implemenat stuffing function
      // TODO : CANFD_MTU is not used and fix to 1 but if the MTU need to be implemented, the maximum value is #define CANFD_MTU = 72 in linux/can.h 
      // FLAGS this instantce are not used. If addition support is needed please add the management of the FLAGS
      
      // END TODO
      int i = send(hw->m_SocketFd,&frameFD,sizeof(struct canfd_frame),requiered_mtu);   //works for CAN_FD frame   
      info.GetReturnValue().Set(i);
  }
  /**
   * Set a list of active filters to be applied for incoming messages
   * @method setRxFilters
   * @param filters {Object} single filter or array of filter e.g. { id: 0x1ff, mask: 0x1ff, invert: false}, result of (id & mask)
   */
  static NAN_METHOD(SetRxFilters)
  {
    RawChannel* hw = ObjectWrap::Unwrap<RawChannel>(info.Holder());

    CHECK_CONDITION(info.Length() > 0, "Too few arguments");
    CHECK_CONDITION(info[0]->IsArray() || info[0]->IsObject(), "Invalid argument");

    CHECK_CONDITION(hw->IsValid(), "Channel not ready");

    struct can_filter *rfilter;
    int numfilter = 0;

    v8::Local<v8::Context> context = info.GetIsolate()->GetCurrentContext();

    if (info[0]->IsArray())
    {
      v8::Local<v8::Array> list = v8::Local<v8::Array>::Cast(info[0]);
      size_t idx;

      rfilter = (struct can_filter *)malloc(sizeof(struct can_filter) * list->Length());

      CHECK_CONDITION(rfilter, "Couldn't allocate memory for filter list");

      for (idx = 0; idx < list->Length(); idx++)
      {
        if (ObjectToFilter(context, Nan::To<Object>(list->Get(context, idx).ToLocalChecked()).ToLocalChecked(), &rfilter[numfilter]))
          numfilter++;
      }
    }
    else
    {
      rfilter = (struct can_filter *)malloc(sizeof(struct can_filter));

      CHECK_CONDITION(rfilter, "Couldn't allocate memory for filter list");

      if (ObjectToFilter(context, Nan::To<Object>(info[0]).ToLocalChecked(), &rfilter[numfilter]))
        numfilter++;
    }

    if (numfilter)
      setsockopt(hw->m_SocketFd, SOL_CAN_RAW, CAN_RAW_FILTER, NULL, 0);  
    if (rfilter)
      free(rfilter);

    info.GetReturnValue().Set(info.This());
  }

  /**
   * Set a list of active filters to be applied for errors
   * @method setErrorFilters
   * @param errorMask {Uint32} CAN error mask
   */
  static NAN_METHOD(SetErrorFilters)
  {
    RawChannel* hw = ObjectWrap::Unwrap<RawChannel>(info.Holder());

    CHECK_CONDITION(info.Length() > 0, "Too few arguments");
    CHECK_CONDITION(info[0]->IsUint32(), "Invalid argument");
    CHECK_CONDITION(hw->IsValid(), "Channel not ready");

    v8::Local<v8::Context> context = info.GetIsolate()->GetCurrentContext();

    can_err_mask_t err_mask = (can_err_mask_t) info[0]->ToUint32(context).ToLocalChecked()->Value();

    setsockopt(hw->m_SocketFd, SOL_CAN_RAW, CAN_RAW_FILTER, NULL, 0);  
    info.GetReturnValue().Set(info.This());
  }

  /**
   * Disable loopback of channel. By default it is activated
   * @method disableLoopback
   */
  static NAN_METHOD(DisableLoopback)
  {
    RawChannel* hw = ObjectWrap::Unwrap<RawChannel>(info.Holder());
    CHECK_CONDITION(hw->IsValid(), "Channel not ready");
    const int loopback = 0;
    setsockopt(hw->m_SocketFd, SOL_CAN_RAW, CAN_RAW_FILTER, NULL, 0);
    info.GetReturnValue().Set(info.This());
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

private:
  uv_async_t m_AsyncReceiverReady;
  uv_async_t m_AsyncChannelStopped;

  struct listener {
      Nan::Persistent<v8::Object> handle;
      Nan::Persistent<v8::Function> callback;
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

  static void * c_thread_entry(void *_this) { assert(_this); reinterpret_cast<RawChannel *>(_this)->ThreadEntry(); return NULL; }

  void ThreadEntry()
  {
    struct pollfd pfd;

    pfd.fd = m_SocketFd;
    pfd.events = POLLIN|POLLHUP|POLLERR;

    while (!m_ThreadStopRequested)
    {
      pfd.revents = 0;

      pthread_mutex_lock(&m_ReadPendingMtx);

      while (unlikely(m_ReadPending && !m_ThreadStopRequested))
      {
        // Read pending and not yet consumed -> wait
        pthread_cond_wait(&m_ReadPendingCond, &m_ReadPendingMtx);
      }

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

  static bool ObjectToFilter(v8::Local<v8::Context> context, v8::Local<v8::Object> object, struct can_filter *rfilter)
  {
    Nan::HandleScope scope;

    v8::Local<v8::Value> id = object->Get(context, id_symbol).ToLocalChecked();
    v8::Local<v8::Value> mask = object->Get(context, mask_symbol).ToLocalChecked();

    if (!id->IsUint32() || !mask->IsUint32())
      return false;

    rfilter->can_id = id->ToUint32(context).ToLocalChecked()->Value();
    rfilter->can_mask = mask->ToUint32(context).ToLocalChecked()->Value();

    if (object->Get(context, invert_symbol).ToLocalChecked()->IsTrue())
      rfilter->can_id |= CAN_INV_FILTER;

    rfilter->can_mask &= ~CAN_ERR_FLAG;

    return true;
  }

  static void async_receiver_ready_cb(uv_async_t* handle)
  {
    assert(handle);
    assert(handle->data);
    reinterpret_cast<RawChannel*>(handle->data)->async_receiver_ready();
  }

  static void async_channel_stopped_cb(uv_async_t* handle)
  {
    assert(handle);
    assert(handle->data);
    reinterpret_cast<RawChannel*>(handle->data)->async_channel_stopped();
  }

  void async_channel_stopped()
  {
    Nan::HandleScope scope;

    {
      Nan::TryCatch try_catch;

      for (size_t i = 0; i < m_OnChannelStoppedListeners.size(); i++)
      {
        struct listener *listener = m_OnChannelStoppedListeners.at(i);

        Nan::Callback callback(Nan::New(listener->callback));
        if (listener->handle.IsEmpty())
          callback.Call(0, NULL);
        else
          callback.Call(Nan::New(listener->handle), 0, NULL);
      }

      if (unlikely(try_catch.HasCaught()))
        Nan::FatalException(try_catch);
    }

    if (m_Thread)
    {
      stopThread();

      uv_close((uv_handle_t *)&m_AsyncReceiverReady, NULL);
      uv_close((uv_handle_t *)&m_AsyncChannelStopped, NULL);
    }

    Unref();
  }

  void async_receiver_ready()
  {
    Nan::HandleScope scope;

    struct canfd_frame framefd;         // for CANFD frame
    struct can_frame frame;             // for CAN frame
    
    unsigned int framesProcessed = 0;
    
    if (Flag_CANFD_Used){               // standard CAN frame
      while (recv(m_SocketFd, &framefd, sizeof(struct canfd_frame), MSG_DONTWAIT) > 0)      // go modif : use CAN_FD struct
      {
        Nan::TryCatch try_catch;

        v8::Local<v8::Object> obj = Nan::New<v8::Object>();

        canid_t id = framefd.can_id;
        bool isEff = framefd.can_id & CAN_EFF_FLAG;
        bool isRtr = framefd.can_id & CAN_RTR_FLAG;
        bool isErr = framefd.can_id & CAN_ERR_FLAG;

        id = isEff ? framefd.can_id & CAN_EFF_MASK : framefd.can_id & CAN_SFF_MASK;

        v8::Local<v8::Value> argv[] = {
          obj,
        };

        if (m_TimestampsSupported)
        {
          struct timeval tv;

          if (likely(ioctl(m_SocketFd, SIOCGSTAMP, &tv) >= 0))
          {
            Nan::Set(obj, tssec_symbol, Nan::New((int32_t)tv.tv_sec));
            Nan::Set(obj, tsusec_symbol, Nan::New((int32_t)tv.tv_usec));
          }
        }

        Nan::Set(obj, id_symbol, Nan::New(id));

        if (isEff)
          Nan::Set(obj, ext_symbol, Nan::New(isEff));

        // add info on the canfd_symbol = 1 to inform about the type of frame receive.
        Nan::Set(obj, canfd_symbol, Nan::New(1));;
          
          
        if (isErr)
          Nan::Set(obj, err_symbol, Nan::New(isErr));

        // If the CANFD is used treat the data with 64bytes Max size
          Nan::Set(obj, data_symbol, Nan::CopyBuffer((char *)framefd.data, framefd.len & 0x7f).ToLocalChecked());      // Try to change to CAN_FD struct
        
        for (size_t i = 0; i < m_OnMessageListeners.size(); i++)
        {
          struct listener *listener = m_OnMessageListeners.at(i);
           Nan::Callback callback(Nan::New(listener->callback));
          if (listener->handle.IsEmpty())
            callback.Call(1, argv);
          else
            callback.Call(Nan::New(listener->handle), 1, argv);
        }

        if (unlikely(try_catch.HasCaught()))
          Nan::FatalException(try_catch);

        if (++framesProcessed > MAX_FRAMES_PER_ASYNC_EVENT)
          break;
      }
    }
    else{
      while (recv(m_SocketFd, &frame, sizeof(struct can_frame), MSG_DONTWAIT) > 0)      
      {
        Nan::TryCatch try_catch;

        v8::Local<v8::Object> obj = Nan::New<v8::Object>();

        canid_t id = frame.can_id;
        bool isEff = frame.can_id & CAN_EFF_FLAG;
        bool isRtr = frame.can_id & CAN_RTR_FLAG;
        bool isErr = frame.can_id & CAN_ERR_FLAG;

        id = isEff ? frame.can_id & CAN_EFF_MASK : frame.can_id & CAN_SFF_MASK;

        v8::Local<v8::Value> argv[] = {
          obj,
        };

        if (m_TimestampsSupported)
        {
          struct timeval tv;

          if (likely(ioctl(m_SocketFd, SIOCGSTAMP, &tv) >= 0))
          {
            Nan::Set(obj, tssec_symbol, Nan::New((int32_t)tv.tv_sec));
            Nan::Set(obj, tsusec_symbol, Nan::New((int32_t)tv.tv_usec));
          }
        }

        Nan::Set(obj, id_symbol, Nan::New(id));

        if (isEff)
          Nan::Set(obj, ext_symbol, Nan::New(isEff));

        if (isRtr)
          Nan::Set(obj, rtr_symbol, Nan::New(isRtr));
          
          Nan::Set(obj, canfd_symbol, Nan::New(0));;        // Inform that information received are not CAN_FD

        if (isErr)
          Nan::Set(obj, err_symbol, Nan::New(isErr));

          Nan::Set(obj, data_symbol, Nan::CopyBuffer((char *)frame.data, frame.can_dlc & 0xf).ToLocalChecked()); 
        
        for (size_t i = 0; i < m_OnMessageListeners.size(); i++)
        {
          struct listener *listener = m_OnMessageListeners.at(i);
           Nan::Callback callback(Nan::New(listener->callback));
          if (listener->handle.IsEmpty())
            callback.Call(1, argv);
          else
            callback.Call(Nan::New(listener->handle), 1, argv);
        }

        if (unlikely(try_catch.HasCaught()))
          Nan::FatalException(try_catch);

        if (++framesProcessed > MAX_FRAMES_PER_ASYNC_EVENT)
          break;
      }     
    }
    pthread_mutex_lock(&m_ReadPendingMtx);

    m_ReadPending = false;

    pthread_cond_signal(&m_ReadPendingCond);
    pthread_mutex_unlock(&m_ReadPendingMtx);
  }
};

//-----------------------------------------------------------------------------------------
Nan::Persistent<v8::Function> RawChannel::constructor;

NODE_MODULE(can, RawChannel::Init)
