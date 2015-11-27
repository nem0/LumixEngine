#include "audio_device.h"
#include "clip_manager.h"
#include "engine/engine.h"
#include "engine/iplugin.h"
#include <dsound.h>


namespace Lumix
{


struct AudioDeviceImpl : public AudioDevice
{
	Engine* m_engine;
	HMODULE m_library;
	LPDIRECTSOUND8 m_direct_sound;
	LPDIRECTSOUNDBUFFER m_primary_buffer;
	LPDIRECTSOUND3DLISTENER8 m_listener;


	AudioDeviceImpl()
	{
		m_library = nullptr;
		m_engine = nullptr;
		m_direct_sound = nullptr;
		m_primary_buffer = nullptr;
		m_listener = nullptr;
	}


	bool initPrimaryBuffer()
	{
		DSBUFFERDESC primary_buffer_desc = {};
		primary_buffer_desc.dwSize = sizeof(primary_buffer_desc);
		primary_buffer_desc.dwFlags = DSBCAPS_PRIMARYBUFFER | DSBCAPS_CTRLVOLUME | DSBCAPS_CTRL3D;
		primary_buffer_desc.guid3DAlgorithm = DS3DALG_DEFAULT;
		auto result = SUCCEEDED(
			m_direct_sound->CreateSoundBuffer(&primary_buffer_desc, &m_primary_buffer, nullptr));
		if (!result) return false;

		WAVEFORMATEX wave_format = {};
		wave_format.cbSize = 0;
		wave_format.nChannels = 2;
		wave_format.nSamplesPerSec = 44100;
		wave_format.wBitsPerSample = 16;
		wave_format.nBlockAlign = wave_format.nChannels * wave_format.wBitsPerSample / 8;
		wave_format.nAvgBytesPerSec = wave_format.nSamplesPerSec * wave_format.nBlockAlign;
		wave_format.wFormatTag = WAVE_FORMAT_PCM;

		result = SUCCEEDED(m_primary_buffer->SetFormat(&wave_format));
		if (!result) return false;

		result = SUCCEEDED(
			m_primary_buffer->QueryInterface(IID_IDirectSound3DListener8, (void**)&m_listener));
		if (!result) return false;

		m_listener->SetDopplerFactor(1.0, DS3D_DEFERRED);
		m_listener->SetDistanceFactor(1.0f, DS3D_DEFERRED);
		m_listener->SetRolloffFactor(1.0f, DS3D_DEFERRED);
		m_primary_buffer->Play(0, 0, DSBPLAY_LOOPING);

		return true;
	}


	bool init(Engine& engine)
	{
		m_engine = &engine;

		auto result = SUCCEEDED(CoInitialize(nullptr));
		ASSERT(result);

		m_library = LoadLibrary("dsound.dll");
		if (!m_library) return false;
		auto* dsoundCreate =
			(decltype(DirectSoundCreate8)*)GetProcAddress(m_library, "DirectSoundCreate8");
		if (!dsoundCreate)
		{
			ASSERT(false);
			FreeLibrary(m_library);
			return false;
		}

		result = SUCCEEDED(dsoundCreate(0, &m_direct_sound, nullptr));
		if (!result)
		{
			ASSERT(false);
			FreeLibrary(m_library);
			return false;
		}

		HWND hwnd = (HWND)engine.getPlatformData().window_handle;
		result = SUCCEEDED(m_direct_sound->SetCooperativeLevel(hwnd, DSSCL_PRIORITY));
		if (!result || !initPrimaryBuffer())
		{
			ASSERT(false);
			m_direct_sound->Release();
			FreeLibrary(m_library);
			return false;
		}

		return true;
	}


	~AudioDeviceImpl()
	{
		if (m_direct_sound) m_direct_sound->Release();
		if (m_library) FreeLibrary(m_library);
	}


	BufferHandle createBuffer(const void* data,
		int size_bytes,
		int channels,
		int sample_rate,
		int flags) override
	{
		DSBUFFERDESC desc = {};
		LPDIRECTSOUNDBUFFER buffer;
		desc.dwSize = sizeof(desc);
		desc.dwFlags = DSBCAPS_CTRLVOLUME | DSBCAPS_GETCURRENTPOSITION2 | DSBCAPS_CTRLFREQUENCY |
					   DSBCAPS_CTRLFX;
		bool is_3d = (flags & (int)BufferFlags::IS3D) != 0;
		if (is_3d) desc.dwFlags |= DSBCAPS_CTRL3D;
		desc.dwBufferBytes = size_bytes;

		WAVEFORMATEX wave_format = {};
		wave_format.cbSize = 0;
		wave_format.nChannels = channels;
		wave_format.nSamplesPerSec = sample_rate;
		wave_format.wBitsPerSample = 16;
		wave_format.nBlockAlign = wave_format.nChannels * wave_format.wBitsPerSample / 8;
		wave_format.nAvgBytesPerSec = wave_format.nSamplesPerSec * wave_format.nBlockAlign;
		wave_format.wFormatTag = WAVE_FORMAT_PCM;

		desc.lpwfxFormat = &wave_format;
		auto result = SUCCEEDED(m_direct_sound->CreateSoundBuffer(&desc, &buffer, nullptr));
		if (!result) return nullptr;

		void* p1;
		void* p2;
		DWORD s1, s2;
		result = SUCCEEDED(buffer->Lock(0, size_bytes, &p1, &s1, &p2, &s2, 0));
		if (!result)
		{
			buffer->Release();
			return nullptr;
		}
		memcpy(p1, data, s1);
		result = SUCCEEDED(buffer->Unlock(p1, s1, p2, s2));
		if (!result)
		{
			buffer->Release();
			return nullptr;
		}
		result = SUCCEEDED(buffer->SetCurrentPosition(0));
		if (!result)
		{
			buffer->Release();
			return nullptr;
		}

		if (is_3d)
		{
			LPDIRECTSOUND3DBUFFER8 source;
			if (SUCCEEDED(buffer->QueryInterface(IID_IDirectSound3DBuffer8, (void**)&source)))
			{
				source->SetMaxDistance(10000, DS3D_DEFERRED);
				source->SetMinDistance(2, DS3D_DEFERRED);
				source->SetMode(DS3DMODE_NORMAL, DS3D_DEFERRED);
			}
		}

		return buffer;
	}


	void setEcho(BufferHandle handle,
		float wet_dry_mix,
		float feedback,
		float left_delay,
		float right_delay) override
	{
		auto buffer = (LPDIRECTSOUNDBUFFER)handle;
		DSEFFECTDESC echo_effect;
		memset(&echo_effect, 0, sizeof(DSEFFECTDESC));
		echo_effect.dwSize = sizeof(DSEFFECTDESC);
		echo_effect.guidDSFXClass = GUID_DSFX_STANDARD_ECHO;
		IDirectSoundBuffer8* buffer8;
		if (FAILED(buffer->QueryInterface(IID_IDirectSoundBuffer8, (void**)&buffer8))) return;

		DWORD res = 0;
		if (FAILED(buffer8->SetFX(1, &echo_effect, &res))) return;

		IDirectSoundFXEcho8* echo = NULL;
		DSFXEcho echo_params;
		if (FAILED(buffer8->GetObjectInPath(
			GUID_DSFX_STANDARD_ECHO, 0, IID_IDirectSoundFXEcho8, (LPVOID*)&echo)))
		{
			return;
		}
		if (FAILED(echo->GetAllParameters(&echo_params))) return;
		
		echo_params.fFeedback = feedback;
		echo_params.fWetDryMix = wet_dry_mix;
		echo_params.fRightDelay = right_delay;
		echo_params.fLeftDelay = left_delay;
		echo_params.lPanDelay = DSFXECHO_PANDELAY_MIN;

		echo->SetAllParameters(&echo_params);
	}


	void destroyBuffer(BufferHandle clip) override
	{
		auto buffer = (LPDIRECTSOUNDBUFFER)clip;
		buffer->Release();
	}


	bool isPlaying(BufferHandle clip) override
	{
		auto buffer = (LPDIRECTSOUNDBUFFER)clip;
		DWORD status;
		if (FAILED(buffer->GetStatus(&status))) return false;

		return status & DSBSTATUS_PLAYING;
	}


	void play(BufferHandle clip, bool looped) override
	{
		auto buffer = (LPDIRECTSOUNDBUFFER)clip;
		buffer->Play(0, 0, looped ? DSBPLAY_LOOPING : 0);
	}


	void stop(BufferHandle clip) override
	{
		auto buffer = (LPDIRECTSOUNDBUFFER)clip;
		buffer->Stop();
		buffer->Release();
	}


	void pause(BufferHandle clip) override
	{
		auto buffer = (LPDIRECTSOUNDBUFFER)clip;
		buffer->Stop();
	}


	void setVolume(BufferHandle clip, float volume) override
	{
		auto buffer = (LPDIRECTSOUNDBUFFER)clip;
		buffer->SetVolume(DSBVOLUME_MIN + LONG(volume * (DSBVOLUME_MAX - DSBVOLUME_MIN)));
	}


	void setFrequency(BufferHandle clip, float frequency) override
	{
		auto buffer = (LPDIRECTSOUNDBUFFER)clip;
		buffer->SetFrequency(
			DSBFREQUENCY_MIN + DWORD(frequency * (DSBFREQUENCY_MAX - DSBFREQUENCY_MIN)));
	}


	void setCurrentTime(BufferHandle clip, float time_seconds) override
	{
		auto buffer = (LPDIRECTSOUNDBUFFER)clip;
		WAVEFORMATEX format;
		if (SUCCEEDED(buffer->GetFormat(&format, sizeof(format), nullptr)))
		{
			DWORD pos = DWORD(format.nAvgBytesPerSec * time_seconds);
			buffer->SetCurrentPosition(pos);
		}
	}


	void update(float) override { m_listener->CommitDeferredSettings(); }


	void setSourcePosition(BufferHandle clip, float x, float y, float z) override
	{
		LPDIRECTSOUND3DBUFFER8 source;
		auto buffer = (LPDIRECTSOUNDBUFFER)clip;
		if (SUCCEEDED(buffer->QueryInterface(IID_IDirectSound3DBuffer8, (void**)&source)))
		{
			source->SetPosition(x, y, z, DS3D_DEFERRED);
		}
	}


	void setListenerOrientation(float front_x,
		float front_y,
		float front_z,
		float up_x,
		float up_y,
		float up_z) override
	{
		m_listener->SetOrientation(front_x, front_y, front_z, up_x, up_y, up_z, DS3D_DEFERRED);
	}


	void setListenerPosition(float x, float y, float z) override
	{
		m_listener->SetPosition(x, y, z, DS3D_DEFERRED);
	}
};


AudioDevice* AudioDevice::create(Engine& engine)
{
	auto* device = LUMIX_NEW(engine.getAllocator(), AudioDeviceImpl);
	if (!device->init(engine))
	{
		LUMIX_DELETE(engine.getAllocator(), device);
		return nullptr;
	}
	return device;
}


void AudioDevice::destroy(AudioDevice& device)
{
	LUMIX_DELETE(static_cast<AudioDeviceImpl&>(device).m_engine->getAllocator(), &device);
}


const AudioDevice::BufferHandle AudioDevice::INVALID_BUFFER_HANDLE = nullptr;


} // namespace Lumix