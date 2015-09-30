/* Copyright Sebastian Haas <sebastian@sebastianhaas.info>. All rights reserved.
 * updated for NodeJs 4.X using NAN by Daniel Gross <dgross@intronik.de>
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

#include <vector>
#include <string>

using namespace std;

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

/**
 * Basic CAN access
 * @module CAN
 */

//-----------------------------------------------------------------------------------------
/**
 * A Raw channel to access a certain CAN channel (e.g. vcan0) via CAN messages.
 * @class RawChannel
 */
class RawChannel : public Nan::ObjectWrap {
private:
	static Nan::Persistent<v8::Function> constructor;
public:
	static void Init(v8::Local<v8::Object> exports)  {
		Nan::HandleScope scope;

		// Prepare constructor template
		v8::Local<v8::FunctionTemplate> tpl = Nan::New<v8::FunctionTemplate>(New);
		tpl->SetClassName(Nan::New("Channel").ToLocalChecked());	// FIXME: why is here Channel instead RawChannel used
		tpl->InstanceTemplate()->SetInternalFieldCount(1);	// for storing (this)

		// Prototype
		Nan::SetPrototypeMethod(tpl, "addListener",  	AddListener);
		Nan::SetPrototypeMethod(tpl, "start",        	Start);
		Nan::SetPrototypeMethod(tpl, "stop",         	Stop);
		Nan::SetPrototypeMethod(tpl, "send",         	Send);
		Nan::SetPrototypeMethod(tpl, "setRxFilters", 	SetRxFilters);
		Nan::SetPrototypeMethod(tpl, "disableLoopback", DisableLoopback);
		
		// constructor
		constructor.Reset(tpl->GetFunction());
		exports->Set(Nan::New("RawChannel").ToLocalChecked(), tpl->GetFunction());
	}
private:
	explicit RawChannel(const char *name, bool timestamps = false) : m_Thread(0), m_Name(name), m_SocketFd(-1)
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

	~RawChannel() {
		for (size_t i = 0; i < m_Listeners.size(); i++)
			delete m_Listeners.at(i);

		m_Listeners.clear();

		if (m_SocketFd >= 0)
			close(m_SocketFd);

		if (m_Thread)
			pthread_join(m_Thread, NULL);
	}

    /**
     * Create a new CAN channel object
     * @constructor RawChannel
     * @param interface {string} interface name to create channel on (e.g. can0)
     * @return new RawChannel object
     */
	static NAN_METHOD(New) {	
		bool timestamps = false;

		CHECK_CONDITION(info.IsConstructCall(), "Must be called with new");
		CHECK_CONDITION(info.Length() >= 1, "Too few arguments");
		CHECK_CONDITION(info[0]->IsString(), "First argument must be a string");

		Nan::Utf8String ascii(info[0]->ToString());

		if (info.Length() >= 2)
		{
			if (info[1]->IsBoolean())
				timestamps = info[1]->IsTrue();
		}
				
		RawChannel* hw = new RawChannel(*ascii, timestamps);
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
		Nan::HandleScope scope;
		RawChannel* hw = Nan::ObjectWrap::Unwrap<RawChannel>(info.This());

		CHECK_CONDITION(info.Length() >= 2, "Too few arguments");

		CHECK_CONDITION(info[0]->IsString(), "First argument must be a string");

		Nan::Persistent<v8::Function> func;
		Nan::Persistent<v8::Object> object;

		if (info[1]->IsFunction())
			func.Reset(info[1].As<v8::Function>());

		if (info.Length() >= 3)
		{
			if (info[2]->IsObject())
				object.Reset(info[2]->ToObject());
		}

		
		struct listener *listener = new struct listener;
		listener->handle = object;
		listener->callback = func;

		hw->m_Listeners.push_back(listener);
		
		info.GetReturnValue().Set(info.This());
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
		hw->m_ThreadStopRequested = true;
		pthread_join(hw->m_Thread, NULL);
		hw->m_Thread = 0;
		uv_close((uv_handle_t *)&hw->m_AsyncReceiverReady, NULL);
		hw->Unref();
		info.GetReturnValue().Set(info.This());
	}
	
	/**
	 * Send a CAN message immediately
	 * @method send
	 * @param message {Object} JSON object describing the CAN message, keys are id, length, data {Buffer}, ext or rtr
	 */
	static NAN_METHOD(Send) {
		RawChannel* hw = ObjectWrap::Unwrap<RawChannel>(info.Holder());

		CHECK_CONDITION(info.Length() >= 1, "Invalid arguments");
		CHECK_CONDITION(info[0]->IsObject(), "First argument must be an Object");
		CHECK_CONDITION(hw->IsValid(), "Invalid channel!");
		struct can_frame frame;
		v8::Local<v8::Object> obj =  info[0]->ToObject();

		// TODO: Check for correct structure of message object
		frame.can_id = obj->Get(id_symbol)->Uint32Value();

		if (obj->Get(ext_symbol)->IsTrue())
			frame.can_id |= CAN_EFF_FLAG;

		if (obj->Get(rtr_symbol)->IsTrue())
			frame.can_id |= CAN_RTR_FLAG;

		v8::Local<v8::Value> dataArg = obj->Get(data_symbol);

		CHECK_CONDITION(node::Buffer::HasInstance(dataArg), "Data field must be a Buffer");

		// Get frame data
		frame.can_dlc = node::Buffer::Length(dataArg->ToObject());
		memcpy(frame.data, node::Buffer::Data(dataArg->ToObject()), frame.can_dlc);

		{ // set time stamp when sending data
		  struct timeval now;
		  if ( 0==gettimeofday(&now, 0)) {
			  Nan::Set(obj, tssec_symbol, Nan::New((int32_t)now.tv_sec));
			  Nan::Set(obj, tsusec_symbol, Nan::New((int32_t)now.tv_usec));      
		  }
		}
		
		int i = send(hw->m_SocketFd, &frame, sizeof(struct can_frame), 0);
		
		info.GetReturnValue().Set(i);
	}
	
    /**
     * Set a list of active filters to be applied for incoming messages
     * @method setRxFilters
     * @param filters {Object} single filter or array of filter e.g. { id: 0x1ff, mask: 0x1ff, invert: false}, result of (id & mask)
     */
	static NAN_METHOD(SetRxFilters) {
		RawChannel* hw = ObjectWrap::Unwrap<RawChannel>(info.Holder());
		
		CHECK_CONDITION(info.Length() > 0, "Too few arguments");
		CHECK_CONDITION(info[0]->IsArray() || info[0]->IsObject(), "Invalid argument");

		CHECK_CONDITION(hw->IsValid(), "Channel not ready");

		struct can_filter *rfilter;
		int numfilter = 0;

		if (info[0]->IsArray())
		{
			v8::Local<v8::Array> list = v8::Local<v8::Array>::Cast(info[0]);
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

			if (ObjectToFilter(info[0]->ToObject(), &rfilter[numfilter]))
				numfilter++;
		}

		if (numfilter)
			setsockopt(hw->m_SocketFd, SOL_CAN_RAW, CAN_RAW_FILTER, rfilter, numfilter * sizeof(struct can_filter));

		if (rfilter)
			free(rfilter);		
		
		info.GetReturnValue().Set(info.This());		
	}
	
    /**
     * Disable loopback of channel. By default it is activated
     * @method disableLoopback
     */
	static NAN_METHOD(DisableLoopback) {
		RawChannel* hw = ObjectWrap::Unwrap<RawChannel>(info.Holder());
		CHECK_CONDITION(hw->IsValid(), "Channel not ready");
		const int loopback = 0;
		setsockopt(hw->m_SocketFd, SOL_CAN_RAW, CAN_RAW_LOOPBACK, &loopback, sizeof(loopback));
		info.GetReturnValue().Set(info.This());
	}
		
private:
    uv_async_t m_AsyncReceiverReady;
	
    struct listener {
        Nan::Persistent<v8::Object> handle;
        Nan::Persistent<v8::Function> callback;
    };

    vector<struct listener *> m_Listeners;

    pthread_t m_Thread;
    string m_Name;

    int m_SocketFd;
    struct sockaddr_can m_SocketAddr;

    bool m_ThreadStopRequested;
    bool m_TimestampsSupported;

    static void * c_thread_entry(void *_this) { assert(_this); reinterpret_cast<RawChannel *>(_this)->ThreadEntry(); return NULL; }
    
	void ThreadEntry()
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
	
  bool IsValid() { return m_SocketFd >= 0; }

	static bool ObjectToFilter(v8::Handle<v8::Object> object, struct can_filter *rfilter)
	{
		Nan::HandleScope scope;

		v8::Handle<v8::Value> id = object->Get(id_symbol);
		v8::Handle<v8::Value> mask = object->Get(mask_symbol);

		if (!id->IsUint32() || !mask->IsUint32())
			return false;

		rfilter->can_id = id->Uint32Value();
		rfilter->can_mask = mask->Uint32Value();

		if (object->Get(invert_symbol)->IsTrue())
			rfilter->can_id |= CAN_INV_FILTER;

		rfilter->can_mask &= ~CAN_ERR_FLAG;

		return true;
	}
	
	static void async_receiver_ready_cb(uv_async_t* handle, int status)
	{
		assert(handle);
		assert(handle->data);
		reinterpret_cast<RawChannel*>(handle->data)->async_receiver_ready(status);
	}

	void async_receiver_ready(int status)
	{
		Nan::HandleScope scope;

		struct can_frame frame;

		unsigned int framesProcessed = 0;

		while (recv(m_SocketFd, &frame, sizeof(struct can_frame), MSG_DONTWAIT) > 0)
		{
			v8::TryCatch try_catch;

			v8::Local<v8::Object> obj = v8::Object::New();

			canid_t id = frame.can_id;
			bool isEff = frame.can_id & CAN_EFF_FLAG;
			bool isRtr = frame.can_id & CAN_RTR_FLAG;
			bool isErr = frame.can_id & CAN_ERR_FLAG;

			id = isEff ? frame.can_id & CAN_EFF_MASK : frame.can_id & CAN_SFF_MASK;

			v8::Local<v8::Value> argv[1];
			argv[0] = obj;

			if (m_TimestampsSupported)
			{
				struct timeval tv;

				if (likely(ioctl(m_SocketFd, SIOCGSTAMP, &tv) >= 0))
				{
					obj->Set(tssec_symbol, v8::Uint32::NewFromUnsigned(tv.tv_sec), v8::PropertyAttribute(v8::ReadOnly|v8::DontDelete));
					obj->Set(tsusec_symbol, v8::Uint32::NewFromUnsigned(tv.tv_usec), v8::PropertyAttribute(v8::ReadOnly|v8::DontDelete));
				}
			}

			obj->Set(id_symbol, v8::Uint32::New(id), v8::PropertyAttribute(v8::ReadOnly|v8::DontDelete));

			if (isEff)
				obj->Set(ext_symbol, v8::Boolean::New(isEff), v8::PropertyAttribute(v8::ReadOnly|v8::DontDelete));

			if (isRtr)
				obj->Set(rtr_symbol, v8::Boolean::New(isRtr), v8::PropertyAttribute(v8::ReadOnly|v8::DontDelete));

			if (isErr)
				obj->Set(err_symbol, v8::Boolean::New(isErr), v8::PropertyAttribute(v8::ReadOnly|v8::DontDelete));

		v8::Local<node::Buffer> buffer = node::Buffer::New((char *)frame.data, frame.can_dlc & 0xf);

		obj->Set(data_symbol, buffer->handle_, v8::PropertyAttribute(v8::ReadOnly|v8::DontDelete));

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

};

Nan::Persistent<v8::Function> RawChannel::constructor;

void InitAll(v8::Local<v8::Object> exports) {
  RawChannel::Init(exports);
}

NODE_MODULE(can, InitAll)
