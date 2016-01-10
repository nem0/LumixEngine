#include "audio_device.h"
#include "clip_manager.h"
#include "core/log.h"
#include "engine/engine.h"
#include "engine/iplugin.h"
#include <dsound.h>


namespace Lumix
{


struct AudioDeviceImpl : public AudioDevice
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
			g_log_error.log("audio") << "CoInitialize failed. Error code: " << coinitialize_result;
			ASSERT(false);
			return false;
		}

		m_library = LoadLibrary("dsound.dll");
		if (!m_library)
		{
			g_log_error.log("audio") << "Failed to load dsound.dll.";
			return false;
		}
		auto* dsoundCreate =
			(decltype(DirectSoundCreate8)*)GetProcAddress(m_library, "DirectSoundCreate8");
		if (!dsoundCreate)
		{
			g_log_error.log("audio") << "Failed to get DirectSoundCreate8 from dsound.dll.";
			ASSERT(false);
			FreeLibrary(m_library);
			return false;
		}

		auto create_result = dsoundCreate(0, &m_direct_sound, nullptr);
		if (!SUCCEEDED(create_result))
		{
			g_log_error.log("audio") << "Failed to create DirectSound. Error code: " << create_result;
			ASSERT(false);
			FreeLibrary(m_library);
			return false;
		}

		HWND hwnd = (HWND)engine.getPlatformData().window_handle;
		auto result = SUCCEEDED(m_direct_sound->SetCooperativeLevel(hwnd, DSSCL_PRIORITY));
		if (!result || !initPrimaryBuffer())
		{
			g_log_error.log("audio") << "Failed to initialize the primary buffer.";
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

		for (int i = 0; i < lengthOf(m_buffer_map); ++i)
		{
			if (m_buffer_map[i] == INVALID_BUFFER_HANDLE)
			{
				m_buffer_map[i] = m_buffer_count;
				m_buffers[m_buffer_count].handle = buffer;
				m_buffers[m_buffer_count].data = data;
				m_buffers[m_buffer_count].data_size = data_size;
				m_buffers[m_buffer_count].written = buffer_size;
				m_buffers[m_buffer_count].sparse_idx = i;
				m_buffers[m_buffer_count].handle_3d = source;
				m_buffers[m_buffer_count].handle8 = nullptr;
				buffer->QueryInterface(
					IID_IDirectSoundBuffer8, (void**)&m_buffers[m_buffer_count].handle8);
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
					buffer.handle->Play(0, 0, buffer_status & DSBSTATUS_LOOPING);

				}
				return;
			}
			if (FAILED(buffer.handle8->GetObjectInPath(
					GUID_DSFX_STANDARD_ECHO, 0, IID_IDirectSoundFXEcho8, (LPVOID*)&echo)))
			{
				if (buffer_status & DSBSTATUS_PLAYING)
				{
					buffer.handle->Play(0, 0, buffer_status & DSBSTATUS_LOOPING);
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
			buffer.handle->Play(0, 0, buffer_status & DSBSTATUS_LOOPING);
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
		buffer.handle->Play(0, 0, DSBPLAY_LOOPING);
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
		if (buffer.written + s1 > buffer.data_size)
		{
			memcpy(p1, (uint8*)buffer.data + buffer.written, buffer.data_size - buffer.written);
			void* p1_2 = (uint8*)p1 + (buffer.data_size - buffer.written);
			DWORD s1_2 = s1 - (buffer.data_size - buffer.written);
			if (buffer.looped)
			{
				memcpy(p1_2, buffer.data, s1_2);
			}
			else
			{
				ZeroMemory(p1_2, s1_2);
			}
		}
		else
		{
			memcpy(p1, (uint8*)buffer.data + buffer.written, s1);
		}
		buffer.written += s1;
		if (p2)
		{
			if (buffer.written + s2 > buffer.data_size)
			{
				memcpy(p2, (uint8*)buffer.data + buffer.written, buffer.data_size - buffer.written);
				void* p2_2 = (uint8*)p2 + (buffer.data_size - buffer.written);
				DWORD s2_2 = s2 - (buffer.data_size - buffer.written);
				if (buffer.looped)
				{
					memcpy(p2_2, buffer.data, s2_2);
				}
				else
				{
					ZeroMemory(p2_2, s2_2);
				}
			}
			else
			{
				memcpy(p2, (uint8*)buffer.data + buffer.written, s2);
			}
			buffer.written += s2;
		}
		if (buffer.written > buffer.data_size) buffer.written = buffer.written % buffer.data_size;
		if (FAILED(buffer.handle->Unlock(p1, s1, p2, s2)))
		{
			return;
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


	void setSourcePosition(BufferHandle handle, float x, float y, float z) override
	{
		auto buffer = m_buffers[m_buffer_map[handle]].handle_3d;
		if (buffer) buffer->SetPosition(x, y, z, DS3D_DEFERRED);
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


const AudioDevice::BufferHandle AudioDevice::INVALID_BUFFER_HANDLE = -1;


} // namespace Lumix