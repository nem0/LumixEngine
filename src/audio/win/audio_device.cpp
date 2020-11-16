#define NOGDI 
#include <dsound.h>

#include "audio_device.h"
#include "engine/allocator.h"
#include "engine/crt.h"
#include "engine/engine.h"
#include "engine/log.h"
#include "engine/math.h"


namespace Lumix
{


struct AudioDeviceImpl final : AudioDevice
{
	struct Buffer
	{
		LPDIRECTSOUNDBUFFER handle;
		LPDIRECTSOUND3DBUFFER8 handle_3d;
		IDirectSoundBuffer8* handle8;
		const void* data;
		DWORD data_size;
		DWORD written;
		int sparse_idx;
		bool looped;
	};

	Engine* m_engine;
	HMODULE m_library;
	LPDIRECTSOUND8 m_direct_sound;
	LPDIRECTSOUNDBUFFER m_primary_buffer;
	LPDIRECTSOUND3DLISTENER8 m_listener;
	Buffer m_buffers[MAX_PLAYING_SOUNDS];
	int m_buffer_map[MAX_PLAYING_SOUNDS];
	int m_buffer_count;

	static const int STREAM_SIZE = 32768;

	AudioDeviceImpl()
	{
		m_library = nullptr;
		m_engine = nullptr;
		m_direct_sound = nullptr;
		m_primary_buffer = nullptr;
		m_listener = nullptr;
		m_buffer_count = 0;
		for (auto& i : m_buffer_map)
		{
			i = INVALID_BUFFER_HANDLE;
		}
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

		auto coinitialize_result = CoInitialize(nullptr);
		if (!SUCCEEDED(coinitialize_result))
		{
			logError("CoInitialize failed. Error code: ", (u32)coinitialize_result);
			ASSERT(false);
			return false;
		}

		m_library = LoadLibrary("dsound.dll");
		if (!m_library)
		{
			logError("Failed to load dsound.dll.");
			return false;
		}
		auto* dsoundCreate =
			(decltype(DirectSoundCreate8)*)GetProcAddress(m_library, "DirectSoundCreate8");
		if (!dsoundCreate)
		{
			logError("Failed to get DirectSoundCreate8 from dsound.dll.");
			ASSERT(false);
			FreeLibrary(m_library);
			return false;
		}

		auto create_result = dsoundCreate(0, &m_direct_sound, nullptr);
		if (!SUCCEEDED(create_result))
		{
			logError("Failed to create DirectSound. Error code: ", (u32)create_result);
			ASSERT(false);
			FreeLibrary(m_library);
			return false;
		}

		HWND hwnd = (HWND)engine.getWindowHandle();
		auto result = SUCCEEDED(m_direct_sound->SetCooperativeLevel(hwnd, DSSCL_PRIORITY));
		if (!result || !initPrimaryBuffer())
		{
			logError("Failed to initialize the primary buffer.");
			ASSERT(false);
			m_direct_sound->Release();
			FreeLibrary(m_library);
			return false;
		}

		return true;
	}


	~AudioDeviceImpl()
	{
		if (m_listener) m_listener->Release();
		if (m_primary_buffer) m_primary_buffer->Release();
		if (m_direct_sound) m_direct_sound->Release();
		if (m_library) FreeLibrary(m_library);
	}


	BufferHandle createBuffer(const void* data,
		int data_size,
		int channels,
		int sample_rate,
		int flags) override
	{
		if (m_buffer_count == MAX_PLAYING_SOUNDS) return INVALID_BUFFER_HANDLE;

		int buffer_size = data_size > STREAM_SIZE ? STREAM_SIZE : data_size;
		DSBUFFERDESC desc = {};
		LPDIRECTSOUNDBUFFER buffer;
		desc.dwSize = sizeof(desc);
		desc.dwFlags = DSBCAPS_CTRLVOLUME | DSBCAPS_GETCURRENTPOSITION2 | DSBCAPS_CTRLFREQUENCY |
					   DSBCAPS_CTRLFX;
		bool is_3d = (flags & (int)BufferFlags::IS3D) != 0;
		if (is_3d) desc.dwFlags |= DSBCAPS_CTRL3D;
		desc.dwBufferBytes = buffer_size;

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
		if (!result) return INVALID_BUFFER_HANDLE;

		void* p1;
		void* p2;
		DWORD s1, s2;
		result = SUCCEEDED(buffer->Lock(0, buffer_size, &p1, &s1, &p2, &s2, 0));
		if (!result)
		{
			buffer->Release();
			return INVALID_BUFFER_HANDLE;
		}
		memcpy(p1, data, s1);
		result = SUCCEEDED(buffer->Unlock(p1, s1, p2, s2));
		if (!result)
		{
			buffer->Release();
			return INVALID_BUFFER_HANDLE;
		}
		result = SUCCEEDED(buffer->SetCurrentPosition(0));
		if (!result)
		{
			buffer->Release();
			return INVALID_BUFFER_HANDLE;
		}

		LPDIRECTSOUND3DBUFFER8 source = nullptr;
		if (is_3d)
		{
			if (SUCCEEDED(buffer->QueryInterface(IID_IDirectSound3DBuffer8, (void**)&source)))
			{
				source->SetMaxDistance(10000, DS3D_DEFERRED);
				source->SetMinDistance(2, DS3D_DEFERRED);
				source->SetMode(DS3DMODE_NORMAL, DS3D_DEFERRED);
			}
		}

		for (int& handle : m_buffer_map)
		{
			if (handle == INVALID_BUFFER_HANDLE) {
				const u32 i = u32(&handle - m_buffer_map);
				handle = m_buffer_count;
				m_buffers[m_buffer_count].handle = buffer;
				m_buffers[m_buffer_count].data = data;
				m_buffers[m_buffer_count].data_size = data_size;
				m_buffers[m_buffer_count].written = buffer_size;
				m_buffers[m_buffer_count].sparse_idx = i;
				m_buffers[m_buffer_count].handle_3d = source;
				m_buffers[m_buffer_count].handle8 = nullptr;
				buffer->QueryInterface(IID_IDirectSoundBuffer8, (void**)&m_buffers[m_buffer_count].handle8);
				++m_buffer_count;
				return i;
			}
		}

		ASSERT(false);
		buffer->Release();
		return INVALID_BUFFER_HANDLE;
	}


	void setEcho(BufferHandle handle,
		float wet_dry_mix,
		float feedback,
		float left_delay,
		float right_delay) override
	{
		auto buffer = m_buffers[m_buffer_map[handle]];
		DSEFFECTDESC echo_effect = {};
		echo_effect.dwSize = sizeof(DSEFFECTDESC);
		echo_effect.guidDSFXClass = GUID_DSFX_STANDARD_ECHO;
		if (!buffer.handle8) return;

		DWORD buffer_status;
		if(FAILED(buffer.handle->GetStatus(&buffer_status))) return;
		
		buffer.handle->Stop();
		DWORD res = 0;

		IDirectSoundFXEcho8* echo = NULL;
		if (FAILED(buffer.handle8->GetObjectInPath(
				GUID_DSFX_STANDARD_ECHO, 0, IID_IDirectSoundFXEcho8, (LPVOID*)&echo)))
		{
			if (FAILED(buffer.handle8->SetFX(1, &echo_effect, &res)))
			{
				if (buffer_status & DSBSTATUS_PLAYING)
				{
					buffer.handle->Play(0, 0, buffer_status & DSBSTATUS_LOOPING ? DSBPLAY_LOOPING: 0);

				}
				return;
			}
			if (FAILED(buffer.handle8->GetObjectInPath(
					GUID_DSFX_STANDARD_ECHO, 0, IID_IDirectSoundFXEcho8, (LPVOID*)&echo)))
			{
				if (buffer_status & DSBSTATUS_PLAYING)
				{
					buffer.handle->Play(0, 0, buffer_status & DSBSTATUS_LOOPING ? DSBPLAY_LOOPING : 0);
				}
				return;
			}
		}

		DSFXEcho echo_params;
		
		echo_params.fFeedback = DSFXECHO_FEEDBACK_MIN + feedback * DSFXECHO_FEEDBACK_MAX;
		echo_params.fWetDryMix = DSFXECHO_WETDRYMIX_MIN + wet_dry_mix * DSFXECHO_WETDRYMIX_MAX;
		echo_params.fRightDelay = right_delay;
		echo_params.fLeftDelay = left_delay;
		echo_params.lPanDelay = DSFXECHO_PANDELAY_MIN;

		echo->SetAllParameters(&echo_params);
		if (buffer_status & DSBSTATUS_PLAYING)
		{
			buffer.handle->Play(0, 0, buffer_status & DSBSTATUS_LOOPING ? DSBPLAY_LOOPING : 0);
		}
	}

	void setChorus(BufferHandle handle,
		float wet_dry_mix,
		float depth, 
		float feedback, 
		float frequency, 
		float delay, 
		i32 phase) override
	{
		auto buffer = m_buffers[m_buffer_map[handle]];
		DSEFFECTDESC chorus_effect = {};
		chorus_effect.dwSize = sizeof(DSEFFECTDESC);
		chorus_effect.guidDSFXClass = GUID_DSFX_STANDARD_CHORUS;
		if (!buffer.handle8) return;

		DWORD buffer_status;
		if (FAILED(buffer.handle->GetStatus(&buffer_status))) return;

		buffer.handle->Stop();
		DWORD res = 0;

		IDirectSoundFXChorus8* chorus = NULL;
		if (FAILED(buffer.handle8->GetObjectInPath(
			GUID_DSFX_STANDARD_CHORUS, 0, IID_IDirectSoundFXEcho8, (LPVOID*)&chorus)))
		{
			if (FAILED(buffer.handle8->SetFX(1, &chorus_effect, &res)))
			{
				if (buffer_status & DSBSTATUS_PLAYING)
				{
					buffer.handle->Play(0, 0, buffer_status & DSBSTATUS_LOOPING ? DSBPLAY_LOOPING : 0);

				}
				return;
			}
			if (FAILED(buffer.handle8->GetObjectInPath(
				GUID_DSFX_STANDARD_CHORUS, 0, IID_IDirectSoundFXEcho8, (LPVOID*)&chorus)))
			{
				if (buffer_status & DSBSTATUS_PLAYING)
				{
					buffer.handle->Play(0, 0, buffer_status & DSBSTATUS_LOOPING ? DSBPLAY_LOOPING : 0);
				}
				return;
			}
		}

		DSFXChorus chorus_params;

		chorus_params.fWetDryMix = DSFXCHORUS_WETDRYMIX_MIN + wet_dry_mix * DSFXCHORUS_WETDRYMIX_MAX;
		chorus_params.fDepth = DSFXCHORUS_WETDRYMIX_MIN + depth * DSFXCHORUS_WETDRYMIX_MAX;
		chorus_params.fFeedback = DSFXCHORUS_FEEDBACK_MIN + feedback * DSFXCHORUS_FEEDBACK_MAX;
		chorus_params.fFrequency = DSFXCHORUS_FREQUENCY_MIN + frequency * DSFXCHORUS_FREQUENCY_MAX;
		chorus_params.lWaveform = DSFXCHORUS_WAVE_TRIANGLE;
		chorus_params.fDelay = DSFXCHORUS_DELAY_MIN + delay * DSFXCHORUS_DELAY_MAX;
		chorus_params.lPhase = (phase < DSFXCHORUS_PHASE_MIN) ? DSFXCHORUS_PHASE_MIN : (phase > DSFXCHORUS_PHASE_MAX) ? DSFXCHORUS_PHASE_MAX : phase;

		chorus->SetAllParameters(&chorus_params);
		if (buffer_status & DSBSTATUS_PLAYING)
		{
			buffer.handle->Play(0, 0, buffer_status & DSBSTATUS_LOOPING ? DSBPLAY_LOOPING : 0);
		}
	}

	bool isPlaying(BufferHandle handle) override
	{
		auto buffer = m_buffers[m_buffer_map[handle]].handle;
		DWORD status;
		if (FAILED(buffer->GetStatus(&status))) return false;

		return status & DSBSTATUS_PLAYING;
	}


	void play(BufferHandle handle, bool looped) override
	{
		auto& buffer = m_buffers[m_buffer_map[handle]];
		buffer.looped = looped;
		buffer.handle->Play(0, 0, looped || buffer.data_size > STREAM_SIZE ? DSBPLAY_LOOPING : 0);
	}


	bool isEnd(BufferHandle handle) override
	{
		int dense_idx = m_buffer_map[handle];
		Buffer& buffer = m_buffers[dense_idx];
		DWORD rel_pc, rel_wc;
		DWORD status;
		if (buffer.data_size <= STREAM_SIZE)
		{
			buffer.handle->GetStatus(&status);
			if ((status & DSBSTATUS_PLAYING) == 0) return true;
		}
		buffer.handle->GetCurrentPosition(&rel_pc, &rel_wc);
		auto rel_written = DWORD(buffer.written % STREAM_SIZE);
		DWORD abs_pc = buffer.written - (rel_written - rel_pc);
		if (rel_pc >= rel_written) abs_pc -= STREAM_SIZE;
		return abs_pc >= buffer.data_size;
	}


	void stop(BufferHandle handle) override
	{
		--m_buffer_count;
		int dense_idx = m_buffer_map[handle];

		auto& buffer = m_buffers[dense_idx];
		buffer.handle->Stop();
		if (buffer.handle_3d) buffer.handle_3d->Release();
		if (buffer.handle8) buffer.handle8->Release();
		buffer.handle->Release();

		m_buffers[dense_idx] = m_buffers[m_buffer_count];
		m_buffers[m_buffer_count].handle = nullptr;
		
		m_buffer_map[m_buffers[dense_idx].sparse_idx] = dense_idx;
		m_buffer_map[handle] = -1;
	}


	void pause(BufferHandle handle) override { m_buffers[m_buffer_map[handle]].handle->Stop(); }


	void setMasterVolume(float volume) override
	{
		LONG v = volume < 0.0001f ? DSBVOLUME_MIN : LONG(-2000.0f * log10(1.0f / volume));
		m_primary_buffer->SetVolume(v);
	}


	void setVolume(BufferHandle handle, float volume) override
	{
		m_buffers[m_buffer_map[handle]].handle->SetVolume(
			DSBVOLUME_MIN + LONG(volume * (DSBVOLUME_MAX - DSBVOLUME_MIN)));
	}


	void setFrequency(BufferHandle handle, float frequency) override
	{
		m_buffers[m_buffer_map[handle]].handle->SetFrequency(
			DSBFREQUENCY_MIN + DWORD(frequency * (DSBFREQUENCY_MAX - DSBFREQUENCY_MIN)));
	}


	float getCurrentTime(BufferHandle handle) override
	{
		auto& buffer = m_buffers[m_buffer_map[handle]];

		WAVEFORMATEX format;
		if (SUCCEEDED(buffer.handle->GetFormat(&format, sizeof(format), nullptr)))
		{
			if (buffer.data_size <= STREAM_SIZE)
			{
				DWORD pc, wc;
				buffer.handle->GetCurrentPosition(&pc, &wc);
				return pc / (float)format.nAvgBytesPerSec;
			}
			return buffer.written / (float)format.nAvgBytesPerSec;
		}
		return 0;
	}
	

	void setCurrentTime(BufferHandle handle, float time_seconds) override
	{
		auto& buffer = m_buffers[m_buffer_map[handle]];
		WAVEFORMATEX format;
		if (SUCCEEDED(buffer.handle->GetFormat(&format, sizeof(format), nullptr)))
		{
			DWORD pos = DWORD(format.nAvgBytesPerSec * time_seconds);
			if (pos >= buffer.data_size) pos = 0;
			if (buffer.data_size <= STREAM_SIZE)
			{
				buffer.handle->SetCurrentPosition(pos);
			}
			else
			{
				buffer.written = pos;
			}
		}
	}


	void updateStreamData(Buffer& buffer, DWORD update_size)
	{
		DWORD s1, s2;
		void* p1;
		void* p2;
		if (FAILED(buffer.handle->Lock(buffer.written % STREAM_SIZE, update_size, &p1, &s1, &p2, &s2, 0)))
		{
			return;
		}
		auto updateBuffer = [&buffer](void* p, DWORD size) {
			if (!p) return;
			if (buffer.written + size > buffer.data_size)
			{
				memcpy(p, (u8*)buffer.data + buffer.written, buffer.data_size - buffer.written);
				void* p_2 = (u8*)p + (buffer.data_size - buffer.written);
				DWORD size_2 = size - (buffer.data_size - buffer.written);
				if (buffer.looped)
				{
					memcpy(p_2, buffer.data, size_2);
				}
				else
				{
					ZeroMemory(p_2, size_2);
				}
			}
			else
			{
				memcpy(p, (u8*)buffer.data + buffer.written, size);
			}
			buffer.written += size;
			buffer.written = buffer.written % buffer.data_size;
		};

		updateBuffer(p1, s1);
		updateBuffer(p2, s2);

		if (FAILED(buffer.handle->Unlock(p1, s1, p2, s2)))
		{
			logError("Failed to unlock buffer.");
		}
	}


	void update(float) override 
	{
		for (int i = 0; i < m_buffer_count; ++i)
		{
			auto& buffer = m_buffers[i];
			if (buffer.data_size <= STREAM_SIZE) continue;

			DWORD rel_pc, rel_wc;
			buffer.handle->GetCurrentPosition(&rel_pc, &rel_wc);

			auto rel_written = DWORD(buffer.written % STREAM_SIZE);
			DWORD abs_pc = buffer.written - (rel_written - rel_pc);
			if (rel_pc >= rel_written) abs_pc -= STREAM_SIZE;
			if (buffer.written - abs_pc < STREAM_SIZE / 2)
			{
				DWORD update_size = abs_pc + STREAM_SIZE - buffer.written;
				updateStreamData(buffer, update_size);
			}
		}
		m_listener->CommitDeferredSettings(); 
	}


	void setSourcePosition(BufferHandle handle, const DVec3& pos) override
	{
		auto buffer = m_buffers[m_buffer_map[handle]].handle_3d;
		if (buffer) buffer->SetPosition((float)pos.x, (float)pos.y, (float)pos.z, DS3D_DEFERRED);
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


	void setListenerPosition(const DVec3& pos) override
	{
		m_listener->SetPosition((float)pos.x, (float)pos.y, (float)pos.z, DS3D_DEFERRED);
	}
};


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
	void setFrequency(BufferHandle buffer, float frequency) override {}
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


UniquePtr<AudioDevice> AudioDevice::create(Engine& engine)
{
	UniquePtr<AudioDeviceImpl> device = UniquePtr<AudioDeviceImpl>::create(engine.getAllocator());
	if (!device->init(engine))
	{
		logWarning("Using null device");
		return UniquePtr<NullAudioDevice>::create(engine.getAllocator());
	}
	return device.move();
}


} // namespace Lumix