#include "audio_device.h"
#include "core/array.h"
#include "core/log.h"
#include "engine/engine.h"
#include "engine/plugin.h"
#include "core/log.h"
#include "core/math.h"
#include "core/sync.h"
#include "core/thread.h"
#include "core/os.h"
#include <alsa/asoundlib.h>


namespace Lumix
{


struct AudioTask : Thread
{
	AudioTask(struct AudioDeviceImpl& device, IAllocator& allocator)
		: Thread(allocator)
		, m_device(device)
	{}

	virtual int task() override;
	void handleError(int error_code);

	volatile bool m_finished = false;
	AudioDeviceImpl& m_device;
};


struct AudioDeviceImpl : AudioDevice
{
	struct Buffer
	{
		enum class RuntimeFlags
		{
			READY = 1 << 0,
			PLAYING = 1 << 1,
			LOOPED = 1 << 2
		};

		Buffer(IAllocator& allocator) : data(allocator) {}
		
		Array<u8> data;
		int channels;
		int sample_rate;
		int flags;
		int cursor;
		u8 runtime_flags;
	};


	BufferHandle createBuffer(const void* data,
		int size_bytes,
		int channels,
		int sample_rate,
		int flags) override
	{
		MutexGuard lock(m_mutex);
		ASSERT(flags == 0); // nothing else supported yet
		for(int i = 0, c = m_buffers.size(); i < c; ++i)
		{
			Buffer& buffer = m_buffers[i];
			if((buffer.runtime_flags & (u8)Buffer::RuntimeFlags::READY)) continue;

			buffer.channels = channels;
			buffer.sample_rate = sample_rate;
			buffer.flags = flags;
			buffer.data.resize(size_bytes);
			buffer.runtime_flags = (u8)Buffer::RuntimeFlags::READY;
			buffer.cursor = 0;
			memcpy(&buffer.data[0], data, size_bytes);

			return i;
		}
		return INVALID_BUFFER_HANDLE;
	}


	void setEcho(BufferHandle handle,
		float wet_dry_mix,
		float feedback,
		float left_delay,
		float right_delay) override 
	{
		MutexGuard lock(m_mutex);
		ASSERT(false); // not implemented yet
	}


	void setChorus(BufferHandle handle,
		float wet_dry_mix,
		float depth,
		float feedback,
		float frequency,
		float delay,
		i32 phase) override
	{
		MutexGuard lock(m_mutex);
		ASSERT(false); // not implemented yet
	}


	void mix(u16* output, int size_bytes)
	{
		memset(output, 0, size_bytes);

		MutexGuard lock(m_mutex);
		for (Buffer& buffer : m_buffers)
		{
			if((buffer.runtime_flags & (u8)Buffer::RuntimeFlags::PLAYING) == 0) continue;
			
			mixBuffer(output, size_bytes, buffer);
		}
	}


	void mixBuffer(u16* output, int size_bytes, Buffer& buffer)
	{
		ASSERT(buffer.runtime_flags & (u8)Buffer::RuntimeFlags::PLAYING);
		ASSERT(buffer.channels == 1); // nothing else supported yet
		if (buffer.cursor >= buffer.data.size()) return;
		int total = size_bytes;
		bool is_looped = buffer.runtime_flags & (u8)Buffer::RuntimeFlags::LOOPED;
		do
		{
			int to_copy = minimum(total, buffer.data.size() - buffer.cursor);
			memcpy(output, &buffer.data[buffer.cursor], to_copy);
			buffer.cursor += to_copy;
			if(is_looped) buffer.cursor = buffer.cursor % buffer.data.size();
			total -= to_copy;
		} while(total > 0 && is_looped);
	}

	void play(BufferHandle buffer, bool looped) override 
	{
		MutexGuard lock(m_mutex);
		ASSERT(m_buffers[buffer].runtime_flags & (u8)Buffer::RuntimeFlags::READY);
		m_buffers[buffer].runtime_flags |= (u8)Buffer::RuntimeFlags::PLAYING;
		if(looped)
		{
			m_buffers[buffer].runtime_flags |= (u8)Buffer::RuntimeFlags::LOOPED;
		}
		else
		{
			m_buffers[buffer].runtime_flags &= ~(u8)Buffer::RuntimeFlags::LOOPED;
		}
	}


	bool isPlaying(BufferHandle buffer) override 
	{
		MutexGuard lock(m_mutex);
		ASSERT(m_buffers[buffer].runtime_flags & (u8)Buffer::RuntimeFlags::READY);
		return m_buffers[buffer].runtime_flags & (u8)Buffer::RuntimeFlags::PLAYING;
	}


	void stop(BufferHandle buffer) override
	{
		MutexGuard lock(m_mutex);
		ASSERT(m_buffers[buffer].runtime_flags & (u8)Buffer::RuntimeFlags::READY);
		m_buffers[buffer].runtime_flags &= ~(u8)Buffer::RuntimeFlags::PLAYING;
		m_buffers[buffer].cursor = 0;
	}


	bool isEnd(BufferHandle buffer) override
	{ 
		MutexGuard lock(m_mutex);
		ASSERT(m_buffers[buffer].runtime_flags & (u8)Buffer::RuntimeFlags::READY);
		return m_buffers[buffer].cursor >= m_buffers[buffer].data.size();
	}


	void pause(BufferHandle buffer) override
	{
		MutexGuard lock(m_mutex);
		ASSERT(m_buffers[buffer].runtime_flags & (u8)Buffer::RuntimeFlags::READY);
		m_buffers[buffer].runtime_flags &= ~(u8)Buffer::RuntimeFlags::PLAYING;
	}


	void setMasterVolume(float volume) override 
	{
		MutexGuard lock(m_mutex);
		ASSERT(false); // not implemented yet
	}


	void setVolume(BufferHandle buffer, float volume) override 
	{
		MutexGuard lock(m_mutex);
		ASSERT(m_buffers[buffer].runtime_flags & (u8)Buffer::RuntimeFlags::READY);
		ASSERT(false); // not implemented yet
	}


	void setFrequency(BufferHandle buffer, u32 frequency_hz) override 
	{
		MutexGuard lock(m_mutex);
		ASSERT(m_buffers[buffer].runtime_flags & (u8)Buffer::RuntimeFlags::READY);
		ASSERT(false); // not implemented yet
	}


	void setCurrentTime(BufferHandle handle, float time_seconds) override 
	{
		MutexGuard lock(m_mutex);
		ASSERT(m_buffers[handle].runtime_flags & (u8)Buffer::RuntimeFlags::READY);
		
		Buffer& buffer = m_buffers[handle];
		float length = float(buffer.data.size() / double(buffer.sample_rate * 2 * buffer.channels));
		float rel = time_seconds / length;
		buffer.cursor = rel * buffer.data.size();
		buffer.cursor = clamp(buffer.cursor, 0, buffer.data.size());
	}


	float getCurrentTime(BufferHandle handle) override
	{
		MutexGuard lock(m_mutex);
		ASSERT(m_buffers[handle].runtime_flags & (u8)Buffer::RuntimeFlags::READY);
		
		Buffer& buffer = m_buffers[handle];
		float length = float(buffer.data.size() / double(buffer.sample_rate * 2 * buffer.channels));
		return float(length * double(buffer.cursor) / buffer.data.size());
	}


	void setListenerPosition(const DVec3& pos) override
	{
		MutexGuard lock(m_mutex);
		ASSERT(false); // not implemented yet
	}


	void setListenerOrientation(float front_x,
		float front_y,
		float front_z,
		float up_x,
		float up_y,
		float up_z) override
	{
		MutexGuard lock(m_mutex);
		ASSERT(false); // not implemented yet
	}
	

	void setSourcePosition(BufferHandle buffer, const DVec3& pos) override
	{
		MutexGuard lock(m_mutex);
		ASSERT(m_buffers[buffer].runtime_flags & (u8)Buffer::RuntimeFlags::READY);
		ASSERT(false); // not implemented yet
	}
	
	
	void update(float time_delta) override 
	{
	}


	AudioDeviceImpl(Engine& engine)
		: m_allocator(engine.getAllocator())
		, m_engine(engine)
		, m_buffers(m_allocator)
	{
		m_buffers.reserve(MAX_BUFFERS_COUNT);
		for (int i = 0; i < MAX_BUFFERS_COUNT; ++i)
		{
			Buffer& buffer = m_buffers.emplace(m_allocator);
			buffer.runtime_flags = 0;
		}
	}


	~AudioDeviceImpl()
	{
		if (m_task)
		{
			m_task->m_finished = true;
			m_task->destroy();
			LUMIX_DELETE(m_allocator, m_task);
		}
		if (m_device) m_api.snd_pcm_close(m_device);
		if (m_alsa_lib) os::unloadLibrary(m_alsa_lib);
	}


	bool loadAlsa()
	{
		m_alsa_lib = os::loadLibrary("libasound.so");
		if (!m_alsa_lib) return false;

		#define API(func) \
			do { \
				m_api.func = (decltype(m_api.func))os::getLibrarySymbol(m_alsa_lib, #func);\
				if(!m_api.func)\
				{\
					os::unloadLibrary(m_alsa_lib);\
					m_alsa_lib = nullptr;\
					return false;\
				}\
			} while(false)

		API(snd_pcm_open);
		API(snd_pcm_close);
		API(snd_pcm_start);
		API(snd_pcm_writei);
		API(snd_strerror);
		API(snd_pcm_hw_params);
		API(snd_pcm_hw_params_any);
		API(snd_pcm_hw_params_sizeof);
		API(snd_pcm_hw_params_set_format);
		API(snd_pcm_hw_params_set_channels);
		API(snd_pcm_hw_params_set_rate_near);
		API(snd_pcm_hw_params_set_access);
		API(snd_pcm_hw_params_set_buffer_size_near);
		API(snd_pcm_name);
		API(snd_pcm_state);
		API(snd_pcm_wait);
		API(snd_pcm_avail_update);
		API(snd_pcm_recover);
		API(snd_pcm_reset);
		API(snd_pcm_delay);

		#undef API

		return true;
	}


	bool init()
	{
		if (!loadAlsa()) return false;
		
		unsigned int rate = 44100;
		int channels = 1;
		snd_pcm_hw_params_t* hw_params;
		snd_pcm_uframes_t buffer_size = 1024;

		int res = m_api.snd_pcm_open(&m_device, "default", SND_PCM_STREAM_PLAYBACK, SND_PCM_NONBLOCK);
		if(res < 0) goto error;

		hw_params = (snd_pcm_hw_params_t*)alloca(m_api.snd_pcm_hw_params_sizeof());
		res = m_api.snd_pcm_hw_params_any(m_device, hw_params);
		if(res < 0) goto error;

		if (m_api.snd_pcm_hw_params_set_access(m_device, hw_params, SND_PCM_ACCESS_RW_INTERLEAVED) < 0) goto error;
		if (m_api.snd_pcm_hw_params_set_format(m_device, hw_params, SND_PCM_FORMAT_S16_LE) < 0)  goto error;
		if (m_api.snd_pcm_hw_params_set_channels(m_device, hw_params, channels) < 0) goto error; 
		if (m_api.snd_pcm_hw_params_set_rate_near(m_device, hw_params, &rate, 0) < 0) goto error;
		if (m_api.snd_pcm_hw_params_set_buffer_size_near(m_device, hw_params, &buffer_size) < 0) goto error;
		res = m_api.snd_pcm_hw_params(m_device, hw_params);
		if(res < 0) goto error;
		
		res = m_api.snd_pcm_start(m_device);
		if(res < 0) goto error;

		logInfo("PCM name: '", m_api.snd_pcm_name(m_device), "'");
		logInfo("PCM state: '", m_api.snd_pcm_state(m_device), "'");

		m_task = LUMIX_NEW(m_allocator, AudioTask)(*this, m_allocator);
		m_task->create("AudioTask", true);

		return true;

		error:
			const char* error_msg = m_api.snd_strerror(res);
			logError(error_msg);
			return false;
	}


	struct API
	{
		int	(*snd_pcm_open)(snd_pcm_t** pcm, const char* name, snd_pcm_stream_t stream, int mode);
		int (*snd_pcm_close)(snd_pcm_t* handle);
		int (*snd_pcm_start)(snd_pcm_t* pcm); 	
		int (*snd_pcm_hw_params_any)(snd_pcm_t* pcm, snd_pcm_hw_params_t* params);
		int (*snd_pcm_hw_params)(snd_pcm_t* pcm, snd_pcm_hw_params_t* params);
		const char* (*snd_strerror)(int error_num);
		int (*snd_pcm_delay)(snd_pcm_t* pcm, snd_pcm_sframes_t* delayp); 
		int (*snd_pcm_reset)(snd_pcm_t*	pcm); 	
		int (*snd_pcm_recover)(snd_pcm_t* pcm, int err, int silent);
		size_t (*snd_pcm_hw_params_sizeof)();
		int (*snd_pcm_hw_params_set_access)(snd_pcm_t* pcm, snd_pcm_hw_params_t* params, snd_pcm_access_t _access);
		int (*snd_pcm_hw_params_set_format)(snd_pcm_t* pcm, snd_pcm_hw_params_t* params, snd_pcm_format_t val);
		int (*snd_pcm_hw_params_set_channels)(snd_pcm_t* pcm, snd_pcm_hw_params_t* params, unsigned int val);
		int (*snd_pcm_hw_params_set_rate_near)(snd_pcm_t* pcm, snd_pcm_hw_params_t* params, unsigned int* val, int* dir);
		const char* (*snd_pcm_name)(snd_pcm_t* pcm);
		snd_pcm_state_t (*snd_pcm_state)(snd_pcm_t* pcm);
		int (*snd_pcm_wait)(snd_pcm_t* pcm, int timeout_ms);
		snd_pcm_sframes_t (*snd_pcm_writei)(snd_pcm_t* pcm, const void* buffer, snd_pcm_uframes_t size);
		snd_pcm_sframes_t (*snd_pcm_avail_update)(snd_pcm_t* pcm);
		int (*snd_pcm_hw_params_set_buffer_size_near)(snd_pcm_t* pcm, snd_pcm_hw_params_t* params, snd_pcm_uframes_t* val);
	};


	static const int MAX_BUFFERS_COUNT = 256;


	IAllocator& m_allocator;
	Array<Buffer> m_buffers;
	AudioTask* m_task = nullptr;
	Engine& m_engine;
	Mutex m_mutex;
	void* m_alsa_lib = nullptr;
	snd_pcm_t* m_device = nullptr;
	API m_api;
};


void AudioTask::handleError(int error_code)
{
	const char* error_msg = m_device.m_api.snd_strerror(error_code);
	logError(error_msg);
}


int AudioTask::task()
{
	while(!m_finished)
	{
		u16 buffer[2*1024];
		int frames_avail = lengthOf(buffer);
		m_device.mix(buffer, frames_avail * 2);

		u16* iter = buffer;
		while(frames_avail > 0)
		{		
			snd_pcm_sframes_t frames_written = m_device.m_api.snd_pcm_writei(m_device.m_device, buffer, frames_avail);
			if (frames_written < 0)
			{
				if (frames_written == -EAGAIN) continue;
				if (frames_written == -EPIPE) 
				{
					int recover_result = m_device.m_api.snd_pcm_recover(m_device.m_device, frames_written, 1);
					if (recover_result < 0)
					{
						handleError(recover_result);
						break;
					}

					frames_written = m_device.m_api.snd_pcm_writei(m_device.m_device, buffer, frames_avail);
					if (frames_written < 0)
					{
						handleError(recover_result);
						break;
					}
				} 
				else 
				{
					handleError(frames_written);
				}
			}
			else
			{
				frames_avail -= frames_written;
				iter += frames_written;
			}
		}
	}
	return 0;
}


struct NullAudioDevice final : AudioDevice
{
	BufferHandle createBuffer(const void* data,
		int size_bytes,
		int channels,
		int sample_rate,
		int flags) override
	{
		return INVALID_BUFFER_HANDLE;
	}
	void setEcho(BufferHandle handle,
		float wet_dry_mix,
		float feedback,
		float left_delay,
		float right_delay) override {}
	void setChorus(BufferHandle handle,
		float wet_dry_mix,
		float depth,
		float feedback,
		float frequency,
		float delay,
		i32 phase) override {}
	void play(BufferHandle buffer, bool looped) override {}
	bool isPlaying(BufferHandle buffer) override { return false; }
	void stop(BufferHandle buffer) override {}
	bool isEnd(BufferHandle buffer) override { return true; }
	void pause(BufferHandle buffer) override {}
	void setMasterVolume(float volume) override {}
	void setVolume(BufferHandle buffer, float volume) override {}
	void setFrequency(BufferHandle buffer, u32 frequency_hz) override {}
	void setCurrentTime(BufferHandle buffer, float time_seconds) override {}
	float getCurrentTime(BufferHandle buffer) override { return -1; }
	void setListenerPosition(const DVec3& pos) override {}
	void setListenerOrientation(float front_x,
		float front_y,
		float front_z,
		float up_x,
		float up_y,
		float up_z) override {}
	void setSourcePosition(BufferHandle buffer, const DVec3& pos) override {}
	void update(float time_delta) override {}
};


static NullAudioDevice g_null_device;


UniquePtr<AudioDevice> AudioDevice::create(Engine& engine, IAllocator& allocator)
{
	UniquePtr<AudioDeviceImpl> device = UniquePtr<AudioDeviceImpl>::create(allocator, engine);
	if (!device->init()) {
		logWarning("Using null device");
		return UniquePtr<NullAudioDevice>::create(engine.getAllocator());
	}
	return device;
}



} // namespace Lumix