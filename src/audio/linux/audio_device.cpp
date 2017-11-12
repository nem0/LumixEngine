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
	
	
	void update(float time_delta) override 
	{
		u16 buffer[1024];
		for(int i = 0; i < 1024; ++i) buffer[i] = u16(i % 2 ? 32 : 16000);
		int res = m_api.snd_pcm_writei(m_device, buffer, sizeof(buffer));
		if (res < 0)
		{
			const char* error_msg = m_api.snd_strerror(res);
			g_log_error.log("Audio") << error_msg;
			int e1 = -EBADFD;
			int e2 = -EPIPE;
			int e3 = -ESTRPIPE;
			e1 = 1;
		}
	}


	AudioDeviceImpl(Engine& engine)
		: m_allocator(engine.getAllocator())
		, m_engine(engine)
	{}


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

		#undef API

		return true;
	}


	bool init()
	{
		if (!loadAlsa()) return false;
		
		int res = m_api.snd_pcm_open(&m_device, "default", SND_PCM_STREAM_PLAYBACK, 0);
		if(res < 0) goto error;

		snd_pcm_hw_params_t* hw_params;
		hw_params = (snd_pcm_hw_params_t*)alloca(m_api.snd_pcm_hw_params_sizeof());
		res = m_api.snd_pcm_hw_params_any(m_device, hw_params);
		if(res < 0) goto error;

		res = m_api.snd_pcm_hw_params(m_device, hw_params);
		if(res < 0) goto error;
		
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
		snd_pcm_sframes_t (*snd_pcm_writei)(snd_pcm_t *pcm, const void *buffer, snd_pcm_uframes_t size);
	};


	IAllocator& m_allocator;
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