#include "audio_device.h"
#include "clip_manager.h"
#include "engine/log.h"
#include "engine/engine.h"
#include "engine/iplugin.h"
#include "engine/log.h"
#include "engine/system.h"
#include <alsa/asoundlib.h>


namespace Lumix
{


class AudioDeviceImpl : public AudioDevice
{
public:
	BufferHandle createBuffer(const void* data,
		int size_bytes,
		int channels,
		int sample_rate,
		int flags) override
	{
		ASSERT(flags == 0); // nothing else supported yet
		for(int i = 0, c = m_buffers.size(); i < c; ++i)
		{
			Buffer& buffer = m_buffers[i];
			if(!(buffer.runtime_flags & (u8)Buffer::RuntimeFlags::READY)) continue;

			buffer.channels = channels;
			buffer.sample_rate = sample_rate;
			buffer.flags = flags;
			buffer.data.resize(size_bytes);
			buffer.runtime_flags = (u8)Buffer::RuntimeFlags::READY;
			copyMemory(&buffer.data[0], data, size_bytes);

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
		ASSERT(false); // not implemented yet
	}


	void play(BufferHandle buffer, bool looped) override 
	{
		ASSERT(!looped); // nothing else supported right now
		ASSERT(m_buffers[buffer].runtime_flags & (u8)Buffer::RuntimeFlags::READY);
		m_buffers[buffer].runtime_flags |= (u8)Buffer::RuntimeFlags::PLAYING;
	}


	bool isPlaying(BufferHandle buffer) override 
	{
		ASSERT(m_buffers[buffer].runtime_flags & (u8)Buffer::RuntimeFlags::READY);
		return m_buffers[buffer].runtime_flags & (u8)Buffer::RuntimeFlags::PLAYING;
	}


	void stop(BufferHandle buffer) override
	{
		ASSERT(m_buffers[buffer].runtime_flags & (u8)Buffer::RuntimeFlags::READY);
		ASSERT(false); // not implemented yet
	}


	bool isEnd(BufferHandle buffer) override
	{ 
		ASSERT(m_buffers[buffer].runtime_flags & (u8)Buffer::RuntimeFlags::READY);
		ASSERT(false); // not implemented yet
		return true;
	}


	void pause(BufferHandle buffer) override
	{
		ASSERT(m_buffers[buffer].runtime_flags & (u8)Buffer::RuntimeFlags::READY);
		ASSERT(false); // not implemented yet
	}


	void setMasterVolume(float volume) override 
	{
		ASSERT(false); // not implemented yet
	}


	void setVolume(BufferHandle buffer, float volume) override 
	{
		ASSERT(m_buffers[buffer].runtime_flags & (u8)Buffer::RuntimeFlags::READY);
		ASSERT(false); // not implemented yet
	}


	void setFrequency(BufferHandle buffer, float frequency) override 
	{
		ASSERT(m_buffers[buffer].runtime_flags & (u8)Buffer::RuntimeFlags::READY);
		ASSERT(false); // not implemented yet
	}


	void setCurrentTime(BufferHandle buffer, float time_seconds) override 
	{
		ASSERT(false); // not implemented yet
	}


	float getCurrentTime(BufferHandle buffer) override
	{
		ASSERT(m_buffers[buffer].runtime_flags & (u8)Buffer::RuntimeFlags::READY);
		ASSERT(false); // not implemented yet
		return -1;
	}


	void setListenerPosition(float x, float y, float z) override
	{
		ASSERT(false); // not implemented yet
	}


	void setListenerOrientation(float front_x,
		float front_y,
		float front_z,
		float up_x,
		float up_y,
		float up_z) override
	{
		ASSERT(false); // not implemented yet
	}
	

	void setSourcePosition(BufferHandle buffer, float x, float y, float z) override
	{
		ASSERT(m_buffers[buffer].runtime_flags & (u8)Buffer::RuntimeFlags::READY);
		ASSERT(false); // not implemented yet
	}
	
	
	void update(float time_delta) override 
	{
		u16 buffer[16*1024];
		for(int i = 0; i < 16*1024; ++i) buffer[i] = u16(i % 2 ? 32 : 16000);
		int res = m_api.snd_pcm_wait(m_device, -1);
		if (res < 0) goto error;
		res = m_api.snd_pcm_writei(m_device, buffer, sizeof(buffer) / 2 );
		if (res < 0) goto error;
		return;
		
		error:
			int x = EBADFD;
			int y = EPIPE;
			int z = ESTRPIPE;
			const char* error_msg = m_api.snd_strerror(res);
			g_log_error.log("Audio") << error_msg;
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
		if (m_device) m_api.snd_pcm_close(m_device);
		if (m_alsa_lib) unloadLibrary(m_alsa_lib);
	}


	bool loadAlsa()
	{
		m_alsa_lib = loadLibrary("libasound.so");
		if (!m_alsa_lib) return false;

		#define API(func) \
			do { \
				m_api.func = (decltype(m_api.func))getLibrarySymbol(m_alsa_lib, #func);\
				if(!m_api.func)\
				{\
					unloadLibrary(m_alsa_lib);\
					m_alsa_lib = nullptr;\
					return false;\
				}\
			} while(false)

		API(snd_pcm_open);
		API(snd_pcm_close);
		API(snd_pcm_writei);
		API(snd_strerror);
		API(snd_pcm_hw_params);
		API(snd_pcm_hw_params_any);
		API(snd_pcm_hw_params_sizeof);
		API(snd_pcm_hw_params_set_format);
		API(snd_pcm_hw_params_set_channels);
		API(snd_pcm_hw_params_set_rate_near);
		API(snd_pcm_hw_params_set_access);
		API(snd_pcm_name);
		API(snd_pcm_state);
		API(snd_pcm_wait);

		#undef API

		return true;
	}


	bool init()
	{
		if (!loadAlsa()) return false;
		
		unsigned int rate = 44100;
		int channels = 1;

		int res = m_api.snd_pcm_open(&m_device, "default", SND_PCM_STREAM_PLAYBACK, SND_PCM_NONBLOCK);
		if(res < 0) goto error;

		snd_pcm_hw_params_t* hw_params;
		hw_params = (snd_pcm_hw_params_t*)alloca(m_api.snd_pcm_hw_params_sizeof());
		res = m_api.snd_pcm_hw_params_any(m_device, hw_params);
		if(res < 0) goto error;

		if (m_api.snd_pcm_hw_params_set_access(m_device, hw_params, SND_PCM_ACCESS_RW_INTERLEAVED) < 0) goto error;
		if (m_api.snd_pcm_hw_params_set_format(m_device, hw_params, SND_PCM_FORMAT_S16_LE) < 0)  goto error;
		if (m_api.snd_pcm_hw_params_set_channels(m_device, hw_params, channels) < 0) goto error; 
		if (m_api.snd_pcm_hw_params_set_rate_near(m_device, hw_params, &rate, 0) < 0) goto error;

		res = m_api.snd_pcm_hw_params(m_device, hw_params);
		if(res < 0) goto error;
		
		g_log_info.log("Audio") << "PCM name: '" << m_api.snd_pcm_name(m_device) << "'";
		g_log_info.log("Audio") << "PCM state: '" << m_api.snd_pcm_state(m_device) << "'";

		return true;

		error:
			const char* error_msg = m_api.snd_strerror(res);
			g_log_error.log("Audio") << error_msg;
			return false;
	}


	struct API
	{
		int	(*snd_pcm_open)(snd_pcm_t **pcm, const char *name, snd_pcm_stream_t stream, int mode);
		int (*snd_pcm_close)(snd_pcm_t *handle);
		int (*snd_pcm_hw_params_any)(snd_pcm_t* pcm, snd_pcm_hw_params_t* params);
		int (*snd_pcm_hw_params)(snd_pcm_t *pcm, snd_pcm_hw_params_t *params);
		const char* (*snd_strerror)(int error_num);
		size_t (*snd_pcm_hw_params_sizeof)();
		int (*snd_pcm_hw_params_set_access)(snd_pcm_t *pcm, snd_pcm_hw_params_t *params, snd_pcm_access_t _access);
		int (*snd_pcm_hw_params_set_format)(snd_pcm_t *pcm, snd_pcm_hw_params_t *params, snd_pcm_format_t val);
		int (*snd_pcm_hw_params_set_channels)(snd_pcm_t *pcm, snd_pcm_hw_params_t *params, unsigned int val);
		int (*snd_pcm_hw_params_set_rate_near)(snd_pcm_t *pcm, snd_pcm_hw_params_t *params, unsigned int *val, int *dir);
		const char* (*snd_pcm_name)(snd_pcm_t *pcm);
		snd_pcm_state_t (*snd_pcm_state)(snd_pcm_t *pcm);
		int (*snd_pcm_wait)(snd_pcm_t* pcm, int timeout);
		snd_pcm_sframes_t (*snd_pcm_writei)(snd_pcm_t *pcm, const void *buffer, snd_pcm_uframes_t size);
	};


	struct Buffer
	{
		enum class RuntimeFlags
		{
			READY = 1 << 0,
			PLAYING = 1 << 1
		};

		Buffer(IAllocator& allocator) : data(allocator) {}
		
		Array<u8> data;
		int channels;
		int sample_rate;
		int flags;
		u8 runtime_flags;
	};


	static const int MAX_BUFFERS_COUNT = 256;


	IAllocator& m_allocator;
	Array<Buffer> m_buffers;
	Engine& m_engine;
	void* m_alsa_lib = nullptr;
	snd_pcm_t* m_device = nullptr;
	API m_api;
};



class NullAudioDevice LUMIX_FINAL : public AudioDevice
{
public:
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
	void play(BufferHandle buffer, bool looped) override {}
	bool isPlaying(BufferHandle buffer) override { return false; }
	void stop(BufferHandle buffer) override {}
	bool isEnd(BufferHandle buffer) override { return true; }
	void pause(BufferHandle buffer) override {}
	void setMasterVolume(float volume) override {}
	void setVolume(BufferHandle buffer, float volume) override {}
	void setFrequency(BufferHandle buffer, float frequency) override {}
	void setCurrentTime(BufferHandle buffer, float time_seconds) override {}
	float getCurrentTime(BufferHandle buffer) override { return -1; }
	void setListenerPosition(float x, float y, float z) override {}
	void setListenerOrientation(float front_x,
		float front_y,
		float front_z,
		float up_x,
		float up_y,
		float up_z) override {}
	void setSourcePosition(BufferHandle buffer, float x, float y, float z) override {}
	void update(float time_delta) override {}
};


static NullAudioDevice g_null_device;


AudioDevice* AudioDevice::create(Engine& engine)
{
	auto* device = LUMIX_NEW(engine.getAllocator(), AudioDeviceImpl)(engine);
	if (!device->init())
	{
		LUMIX_DELETE(engine.getAllocator(), device);
		g_log_warning.log("Audio") << "Using null device";
		return &g_null_device;
	}
	return device;
}


void AudioDevice::destroy(AudioDevice& device)
{
	if (&device == &g_null_device) return;
	LUMIX_DELETE(static_cast<AudioDeviceImpl&>(device).m_engine.getAllocator(), &device);
}


} // namespace Lumix