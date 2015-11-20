#include "audio_device.h"
#include "core/path.h"
#include "engine/engine.h"
#include "engine/iplugin.h"
#include <dsound.h>


namespace Lumix
{


namespace Audio
{


struct
{
	HMODULE library;
	LPDIRECTSOUND direct_sound;
	bool initialized;
	LPDIRECTSOUNDBUFFER primary_buffer;
} g_audio_device;


static bool initPrimaryBuffer()
{
	DSBUFFERDESC primary_buffer_desc = {};
	primary_buffer_desc.dwSize = sizeof(primary_buffer_desc);
	primary_buffer_desc.dwFlags = DSBCAPS_PRIMARYBUFFER | DSBCAPS_CTRL3D;
	auto result = SUCCEEDED(g_audio_device.direct_sound->CreateSoundBuffer(
		&primary_buffer_desc, &g_audio_device.primary_buffer, nullptr));
	if (!result) return false;

	WAVEFORMATEX wave_format = {};
	wave_format.cbSize = 0;
	wave_format.nChannels = 2;
	wave_format.nSamplesPerSec = 24 * 1024;
	wave_format.wBitsPerSample = 16;
	wave_format.nBlockAlign = wave_format.nChannels * wave_format.wBitsPerSample / 8;
	wave_format.nAvgBytesPerSec = wave_format.nSamplesPerSec * wave_format.nBlockAlign;
	wave_format.wFormatTag = WAVE_FORMAT_PCM;

	result = SUCCEEDED(g_audio_device.primary_buffer->SetFormat(&wave_format));
	if (!result) return false;

	return true;
}


bool init(Engine& engine, IAllocator& allocator)
{
	ASSERT(!g_audio_device.initialized);

	g_audio_device.library = LoadLibrary("dsound.dll");
	if (!g_audio_device.library) return false;
	decltype(DirectSoundCreate)* dsoundCreate =
		(decltype(DirectSoundCreate)*)GetProcAddress(g_audio_device.library, "DirectSoundCreate");
	if (!dsoundCreate)
	{
		ASSERT(false);
		FreeLibrary(g_audio_device.library);
		return false;
	}

	auto result = SUCCEEDED(dsoundCreate(0, &g_audio_device.direct_sound, nullptr));
	if (!result)
	{
		ASSERT(false);
		FreeLibrary(g_audio_device.library);
		return false;
	}

	HWND hwnd = (HWND)engine.getPlatformData().window_handle;
	result = SUCCEEDED(g_audio_device.direct_sound->SetCooperativeLevel(hwnd, DSSCL_PRIORITY));
	if (!result || !initPrimaryBuffer())
	{
		ASSERT(false);
		g_audio_device.direct_sound->Release();
		FreeLibrary(g_audio_device.library);
		return false;
	}

	g_audio_device.initialized = true;
	return true;
}


void shutdown()
{
	ASSERT(g_audio_device.initialized);
	g_audio_device.direct_sound->Release();
	FreeLibrary(g_audio_device.library);
	g_audio_device.initialized = false;
}


ClipHandle load(const Path& path, const void* data, int size, int channels, int sample_rate, int flags)
{
	DSBUFFERDESC desc = {};
	LPDIRECTSOUNDBUFFER buffer;
	desc.dwSize = sizeof(desc);
	desc.dwFlags = DSBCAPS_CTRLVOLUME | DSBCAPS_GETCURRENTPOSITION2 | DSBCAPS_CTRLFREQUENCY | DSBCAPS_CTRLFX | DSBCAPS_LOCSOFTWARE;
	if ((flags & (int)ClipFlags::IS3D) != 0) desc.dwFlags = DSBCAPS_CTRL3D;
	desc.dwBufferBytes = size;

	WAVEFORMATEX wave_format = {};
	wave_format.cbSize = 0;
	wave_format.nChannels = channels;
	wave_format.nSamplesPerSec = sample_rate;
	wave_format.wBitsPerSample = 16;
	wave_format.nBlockAlign = wave_format.nChannels * wave_format.wBitsPerSample / 8;
	wave_format.nAvgBytesPerSec = wave_format.nSamplesPerSec * wave_format.nBlockAlign;
	wave_format.wFormatTag = WAVE_FORMAT_PCM;

	desc.lpwfxFormat = &wave_format;
	auto result =
		SUCCEEDED(g_audio_device.direct_sound->CreateSoundBuffer(&desc, &buffer, nullptr));
	if (!result) return nullptr;

	void* p1;
	void* p2;
	DWORD s1, s2;
	result = SUCCEEDED(buffer->Lock(0, size, &p1, &s1, &p2, &s2, 0));
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

	return buffer;
}


void unload(ClipHandle clip)
{
	auto buffer = (LPDIRECTSOUNDBUFFER)clip;
	buffer->Release();
}


void play(ClipHandle clip)
{
	auto buffer = (LPDIRECTSOUNDBUFFER)clip;
	buffer->Play(0, 0, DSBPLAY_LOOPING);
}


void stop(ClipHandle clip)
{
	auto buffer = (LPDIRECTSOUNDBUFFER)clip;
	buffer->Stop();
}


void pause(ClipHandle clip)
{
	auto buffer = (LPDIRECTSOUNDBUFFER)clip;
	buffer->Stop();
}


void setVolume(ClipHandle clip, float volume)
{
	auto buffer = (LPDIRECTSOUNDBUFFER)clip;
	buffer->SetVolume(DSBVOLUME_MIN + LONG(volume * (DSBVOLUME_MAX - DSBVOLUME_MIN)));
}


void setFrequency(ClipHandle clip, float frequency)
{
	auto buffer = (LPDIRECTSOUNDBUFFER)clip;
	buffer->SetFrequency(
		DSBFREQUENCY_MIN + DWORD(frequency * (DSBFREQUENCY_MAX - DSBFREQUENCY_MIN)));
}


void setCurrentPosition(ClipHandle clip, float time_seconds)
{
	auto buffer = (LPDIRECTSOUNDBUFFER)clip;
	WAVEFORMATEX format;
	if (SUCCEEDED(buffer->GetFormat(&format, sizeof(format), nullptr)))
	{
		DWORD pos = DWORD(format.nAvgBytesPerSec * time_seconds);
		buffer->SetCurrentPosition(pos);
	}
}


void setSourcePosition(ClipHandle clip, float x, float y, float z)
{
	LPDIRECTSOUND3DBUFFER source;
	auto buffer = (LPDIRECTSOUNDBUFFER)clip;
	if(SUCCEEDED(buffer->QueryInterface(IID_IDirectSound3DBuffer8, (void**)&source)))
	{
		source->SetPosition(x, y, z, DS3D_DEFERRED);
	}
}


void setListenerPosition(int index, float x, float y, float z)
{
	LPDIRECTSOUND3DBUFFER listener;
	if(SUCCEEDED(g_audio_device.primary_buffer->QueryInterface(IID_IDirectSound3DBuffer8, (void**)&listener)))
	{
		listener->SetPosition(x, y, z, DS3D_DEFERRED);
	}
}


} // namespace Audio


} // namespace Lumix