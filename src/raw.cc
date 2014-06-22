/* Copyright Sebastian Haas <sebastian@sebastianhaas.info>. All rights reserved.
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
#include <stdlib.h>
#include <unistd.h>

#include <sys/types.h>
#include <sys/time.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <sys/poll.h>
#include <net/if.h>

#include <linux/can.h>
#include <linux/can/raw.h>

#include <string.h>

#include "raw.h"

#include <node_buffer.h>

using namespace node;
using namespace v8;
using namespace std;

#define MAX_FRAMES_PER_ASYNC_EVENT 100

#define likely(x)   __builtin_expect( x , 1)
#define unlikely(x) __builtin_expect( x , 0)

//-----------------------------------------------------------------------------------------
Persistent<FunctionTemplate> RawChannel::s_ct;

static Persistent<String> tssec_symbol;
static Persistent<String> tsusec_symbol;
static Persistent<String> id_symbol;
static Persistent<String> mask_symbol;
static Persistent<String> invert_symbol;
static Persistent<String> ext_symbol;
static Persistent<String> rtr_symbol;
static Persistent<String> err_symbol;
static Persistent<String> data_symbol;

RawChannel::RawChannel(const char *name, bool timestamps) : m_Thread(0), m_Name(name), m_SocketFd(-1)
{
    m_SocketFd = socket(PF_CAN, SOCK_RAW, CAN_RAW);
    m_ThreadStopRequested = false;
    m_TimestampsSupported = timestamps;

    if (m_SocketFd > 0)
    {
        can_err_mask_t err_mask;
        struct ifreq ifr;

        memset(&ifr, 0, sizeof(ifr));
        strncpy(ifr.ifr_name, name, IFNAMSIZ - 1);
        if (ioctl(m_SocketFd, SIOCGIFINDEX, &ifr) != 0)
            goto on_error;

        err_mask = CAN_ERR_MASK;
        if (setsockopt(m_SocketFd, SOL_CAN_RAW, CAN_RAW_ERR_FILTER, &err_mask, sizeof(err_mask)) != 0)
            goto on_error;

        memset(&m_SocketAddr, 0, sizeof(m_SocketAddr));
        m_SocketAddr.can_family = PF_CAN;
        m_SocketAddr.can_ifindex = ifr.ifr_ifindex;

        if (bind(m_SocketFd, (struct sockaddr *)&m_SocketAddr, sizeof(m_SocketAddr)) < 0)
            goto on_error;

        return;

on_error:
        close(m_SocketFd);
        m_SocketFd = -1;
    }
}

RawChannel::~RawChannel()
{
    for (size_t i = 0; i < m_Listeners.size(); i++)
        delete m_Listeners.at(i);

    m_Listeners.clear();

    if (m_SocketFd >= 0)
        close(m_SocketFd);

    if (m_Thread)
        pthread_join(m_Thread, NULL);
}

void RawChannel::Init(Handle<Object> target)
{
    HandleScope scope;

    Local<FunctionTemplate> t = FunctionTemplate::New(New);

    s_ct = Persistent<FunctionTemplate>::New(t);
    s_ct->InstanceTemplate()->SetInternalFieldCount(1);
    s_ct->SetClassName(String::NewSymbol("Channel"));

    NODE_SET_PROTOTYPE_METHOD(s_ct, "addListener",  AddListener);
    NODE_SET_PROTOTYPE_METHOD(s_ct, "start",        Start);
    NODE_SET_PROTOTYPE_METHOD(s_ct, "stop",         Stop);
    NODE_SET_PROTOTYPE_METHOD(s_ct, "send",         Send);
    NODE_SET_PROTOTYPE_METHOD(s_ct, "setRxFilters", SetRxFilters);

    target->Set(String::NewSymbol("RawChannel"), s_ct->GetFunction());

    tssec_symbol  = NODE_PSYMBOL("ts_sec");
    tsusec_symbol = NODE_PSYMBOL("ts_usec");
    id_symbol     = NODE_PSYMBOL("id");
    mask_symbol   = NODE_PSYMBOL("mask");
    invert_symbol = NODE_PSYMBOL("invert");
    ext_symbol    = NODE_PSYMBOL("ext");
    rtr_symbol    = NODE_PSYMBOL("rtr");
    err_symbol    = NODE_PSYMBOL("err");
    data_symbol   = NODE_PSYMBOL("data");
}

Handle<Value> RawChannel::New(const Arguments& args)
{
    HandleScope scope;
    bool timestamps = false;

    CHECK_CONDITION(args.Length() >= 1, "Too few arguments");
    CHECK_CONDITION(args[0]->IsString(), "First argument must be a string");

    String::AsciiValue ascii(args[0]->ToString());

    if (args.Length() >= 2)
    {
        if (args[1]->IsBoolean())
            timestamps = args[1]->IsTrue();
    }

    RawChannel* hw = new RawChannel(*ascii, timestamps);
    hw->Wrap(args.This());

    CHECK_CONDITION(hw->IsValid(), "Error while creating channel");

    return scope.Close(args.This());
}

Handle<Value> RawChannel::AddListener(const Arguments& args)
{
    HandleScope scope;

    RawChannel* hw = ObjectWrap::Unwrap<RawChannel>(args.This());

    CHECK_CONDITION(args.Length() >= 2, "Too few arguments");

    CHECK_CONDITION(args[0]->IsString(), "First argument must be a string");

    Persistent<Function> func;
    Persistent<Object> object;

    if (args[1]->IsFunction())
        func = Persistent<Function>::New(args[1].As<Function>());

    if (args.Length() >= 3)
    {
        if (args[2]->IsObject())
            object = Persistent<Object>::New(args[2]->ToObject());
    }

    struct listener *listener = new struct listener;
    listener->handle = object;
    listener->callback = func;

    hw->m_Listeners.push_back(listener);

    return scope.Close(Undefined());
}

static bool ObjectToFilter(Handle<Object> object, struct can_filter *rfilter)
{
    HandleScope scope;

    Handle<Value> id = object->Get(id_symbol);
    Handle<Value> mask = object->Get(mask_symbol);

    if (!id->IsUint32() || !mask->IsUint32())
        return false;

    rfilter->can_id = id->Uint32Value();
    rfilter->can_mask = mask->Uint32Value();

    if (object->Get(invert_symbol)->IsTrue())
        rfilter->can_id |= CAN_INV_FILTER;

    rfilter->can_mask &= ~CAN_ERR_FLAG;

    return true;
}

Handle<Value> RawChannel::SetRxFilters(const Arguments& args)
{
    HandleScope scope;

    RawChannel* hw = ObjectWrap::Unwrap<RawChannel>(args.This());

    CHECK_CONDITION(args.Length() > 0, "Too few arguments");
    CHECK_CONDITION(args[0]->IsArray() || args[0]->IsObject(), "Invalid argument");

    CHECK_CONDITION(hw->IsValid(), "Channel not ready");

    struct can_filter *rfilter;
    int numfilter = 0;

    if (args[0]->IsArray())
    {
        Local<Array> list = Local<Array>::Cast(args[0]);
        size_t idx;

        rfilter = (struct can_filter *)malloc(sizeof(struct can_filter) * list->Length());

        CHECK_CONDITION(rfilter, "Couldn't allocate memory for filter list");

        printf("Provided filters: %d\n", list->Length());

        for (idx = 0; idx < list->Length(); idx++)
        {
            if (ObjectToFilter(list->Get(idx)->ToObject(), &rfilter[numfilter]))
                numfilter++;
        }
    }
    else
    {
        rfilter = (struct can_filter *)malloc(sizeof(struct can_filter));

        CHECK_CONDITION(rfilter, "Couldn't allocate memory for filter list");

        if (ObjectToFilter(args[0]->ToObject(), &rfilter[numfilter]))
            numfilter++;
    }

    if (numfilter)
        setsockopt(hw->m_SocketFd, SOL_CAN_RAW, CAN_RAW_FILTER, rfilter, numfilter * sizeof(struct can_filter));

    if (rfilter)
        free(rfilter);

    return args.This();
}

Handle<Value> RawChannel::Start(const Arguments& args)
{
    HandleScope scope;

    RawChannel* hw = ObjectWrap::Unwrap<RawChannel>(args.This());

    if (!hw->IsValid())
        return ThrowException(Exception::Error(String::New("Cannot start invalid channel")));

    uv_async_init(uv_default_loop(), &hw->m_AsyncReceiverReady, async_receiver_ready_cb);
    hw->m_AsyncReceiverReady.data = hw;

    hw->m_ThreadStopRequested = false;
    pthread_create(&hw->m_Thread, NULL, c_thread_entry, hw);

    if (!hw->m_Thread)
        return ThrowException(Exception::Error(String::New("Errot starting dispatch thread")));

    hw->Ref();

    return args.This();
}

Handle<Value> RawChannel::Stop(const Arguments& args)
{
    HandleScope scope;

    RawChannel* hw = ObjectWrap::Unwrap<RawChannel>(args.This());

    CHECK_CONDITION(hw->m_Thread, "Channel not started");

    hw->m_ThreadStopRequested = true;
    pthread_join(hw->m_Thread, NULL);
    hw->m_Thread = 0;

    uv_close((uv_handle_t *)&hw->m_AsyncReceiverReady, NULL);

    hw->Unref();

    return args.This();
}

/**
 * Send a CAN message
 * TODO: This is currently a synchronous call. Make it asynchronously with optional callback.
 */
Handle<Value> RawChannel::Send(const Arguments& args)
{
    HandleScope scope;

    RawChannel* hw = ObjectWrap::Unwrap<RawChannel>(args.This());

    if (unlikely(args.Length() < 1))
        return ThrowException(Exception::Error(String::New("Invalid arguments")));

    if (unlikely(!args[0]->IsObject()))
        return ThrowException(Exception::Error(String::New("First argument must be an Object")));
    
    if (unlikely(hw->m_SocketFd < 0))
        return ThrowException(Exception::Error(String::New("Invalid channel")));

    struct can_frame frame;

    Local<Object> obj =  args[0]->ToObject();

    // TODO: Check for correct structure of message object

    frame.can_id = obj->Get(id_symbol)->Uint32Value();

    if (obj->Get(ext_symbol)->IsTrue())
        frame.can_id |= CAN_EFF_FLAG;

    if (obj->Get(rtr_symbol)->IsTrue())
        frame.can_id |= CAN_RTR_FLAG;

    Local<Value> dataArg = obj->Get(data_symbol);

    if (!Buffer::HasInstance(dataArg))
        return ThrowException(Exception::Error(String::New("Data field must be a Buffer")));

    // Get frame data
    frame.can_dlc = Buffer::Length(dataArg->ToObject());
    memcpy(frame.data, Buffer::Data(dataArg->ToObject()), frame.can_dlc);

    int i = send(hw->m_SocketFd, &frame, sizeof(struct can_frame), 0);

    return Int32::New(i);
}

void RawChannel::async_receiver_ready(int status)
{
    HandleScope scope;

    struct can_frame frame;

    unsigned int framesProcessed = 0;

    while (recv(m_SocketFd, &frame, sizeof(struct can_frame), MSG_DONTWAIT) > 0)
    {
        TryCatch try_catch;

        Local<Object> obj = Object::New();

        canid_t id = frame.can_id;
        bool isEff = frame.can_id & CAN_EFF_FLAG;
        bool isRtr = frame.can_id & CAN_RTR_FLAG;
        bool isErr = frame.can_id & CAN_ERR_FLAG;

        id = isEff ? frame.can_id & CAN_EFF_MASK : frame.can_id & CAN_SFF_MASK;

        Local<Value> argv[1];
        argv[0] = obj;

        if (m_TimestampsSupported)
        {
            struct timeval tv;

            if (likely(ioctl(m_SocketFd, SIOCGSTAMP, &tv) >= 0))
            {
                obj->Set(tssec_symbol, Uint32::New(tv.tv_sec), PropertyAttribute(ReadOnly|DontDelete));
                obj->Set(tsusec_symbol, Uint32::New(tv.tv_usec), PropertyAttribute(ReadOnly|DontDelete));
            }
        }

        obj->Set(id_symbol, Uint32::New(id), PropertyAttribute(ReadOnly|DontDelete));

        if (isEff)
            obj->Set(ext_symbol, Boolean::New(isEff), PropertyAttribute(ReadOnly|DontDelete));

        if (isRtr)
            obj->Set(rtr_symbol, Boolean::New(isRtr), PropertyAttribute(ReadOnly|DontDelete));

        if (isErr)
            obj->Set(err_symbol, Boolean::New(isErr), PropertyAttribute(ReadOnly|DontDelete));

	Local<Buffer> buffer = Buffer::New((char *)frame.data, frame.can_dlc & 0xf);

	obj->Set(data_symbol, buffer->handle_, PropertyAttribute(ReadOnly|DontDelete));

        for (size_t i = 0; i < m_Listeners.size(); i++)
        {
            struct listener *listener = m_Listeners.at(i);

            if (listener->handle.IsEmpty())
                listener->callback->Call(Context::GetCurrent()->Global(), 1, &argv[0]);
            else
                listener->callback->Call(listener->handle, 1, &argv[0]);
        }

        if (unlikely(try_catch.HasCaught()))
            FatalException(try_catch);

        if (++framesProcessed > MAX_FRAMES_PER_ASYNC_EVENT)
            break;
    }
}

void RawChannel::ThreadEntry()
{
    struct pollfd pfd;

    pfd.fd = m_SocketFd;
    pfd.events = POLLIN;

    while (!m_ThreadStopRequested)
    {
        pfd.revents = 0;

        if (likely(poll(&pfd, 1, 100) >= 0))
        {
            if (likely(pfd.revents & POLLIN))
                uv_async_send(&m_AsyncReceiverReady);
        }
        else
        {
            break;
        }
    }
}

extern "C" {
  static void init (Handle<Object> target)
  {
    RawChannel::Init(target);
  }

  NODE_MODULE(can, init);
}
