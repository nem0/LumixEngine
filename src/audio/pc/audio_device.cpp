#include "audio_device.h"
#include "clip_manager.h"
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
	LPDIRECTSOUND8 direct_sound;
	bool initialized;
	LPDIRECTSOUNDBUFFER primary_buffer;
	LPDIRECTSOUND3DLISTENER8 listener;
} g_audio_device;


static bool initPrimaryBuffer()
{
	DSBUFFERDESC primary_buffer_desc = {};
	primary_buffer_desc.dwSize = sizeof(primary_buffer_desc);
	primary_buffer_desc.dwFlags = DSBCAPS_PRIMARYBUFFER | DSBCAPS_CTRLVOLUME | DSBCAPS_CTRL3D;
	primary_buffer_desc.guid3DAlgorithm = DS3DALG_DEFAULT;
	auto result = SUCCEEDED(g_audio_device.direct_sound->CreateSoundBuffer(
		&primary_buffer_desc, &g_audio_device.primary_buffer, nullptr));
	if (!result) return false;

	WAVEFORMATEX wave_format = {};
	wave_format.cbSize = 0;
	wave_format.nChannels = 2;
	wave_format.nSamplesPerSec = 44100;
	wave_format.wBitsPerSample = 16;
	wave_format.nBlockAlign = wave_format.nChannels * wave_format.wBitsPerSample / 8;
	wave_format.nAvgBytesPerSec = wave_format.nSamplesPerSec * wave_format.nBlockAlign;
	wave_format.wFormatTag = WAVE_FORMAT_PCM;

	result = SUCCEEDED(g_audio_device.primary_buffer->SetFormat(&wave_format));
	if (!result) return false;

	result = SUCCEEDED(g_audio_device.primary_buffer->QueryInterface(
		IID_IDirectSound3DListener8, (void**)&g_audio_device.listener));
	if (!result) return false;

	g_audio_device.listener->SetDopplerFactor(1.0, DS3D_DEFERRED);
	g_audio_device.listener->SetDistanceFactor(1.0f, DS3D_DEFERRED);
	g_audio_device.listener->SetRolloffFactor(1.0f, DS3D_DEFERRED);
	g_audio_device.primary_buffer->Play(0, 0, DSBPLAY_LOOPING);

	return true;
}


bool init(Engine& engine)
{
	ASSERT(!g_audio_device.initialized);

	auto result = SUCCEEDED(CoInitialize(nullptr));
	ASSERT(result);

	g_audio_device.library = LoadLibrary("dsound.dll");
	if (!g_audio_device.library) return false;
	auto* dsoundCreate =
		(decltype(DirectSoundCreate8)*)GetProcAddress(g_audio_device.library, "DirectSoundCreate8");
	if (!dsoundCreate)
	{
		ASSERT(false);
		FreeLibrary(g_audio_device.library);
		return false;
	}

	result = SUCCEEDED(dsoundCreate(0, &g_audio_device.direct_sound, nullptr));
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


BufferHandle createBuffer(const void* data,
	int size_bytes,
	int channels,
	int sample_rate,
	int flags)
{
	DSBUFFERDESC desc = {};
	LPDIRECTSOUNDBUFFER buffer;
	desc.dwSize = sizeof(desc);
	desc.dwFlags =
		DSBCAPS_CTRLVOLUME | DSBCAPS_GETCURRENTPOSITION2 | DSBCAPS_CTRLFREQUENCY | DSBCAPS_CTRLFX;
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
	auto result =
		SUCCEEDED(g_audio_device.direct_sound->CreateSoundBuffer(&desc, &buffer, nullptr));
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


void destroyBuffer(BufferHandle clip)
{
	auto buffer = (LPDIRECTSOUNDBUFFER)clip;
	buffer->Release();
}


void play(BufferHandle clip, bool looped)
{
	auto buffer = (LPDIRECTSOUNDBUFFER)clip;
	buffer->Play(0, 0, looped ? DSBPLAY_LOOPING : 0);
}


void stop(BufferHandle clip)
{
	auto buffer = (LPDIRECTSOUNDBUFFER)clip;
	buffer->Stop();
	buffer->Release();
}


void pause(BufferHandle clip)
{
	auto buffer = (LPDIRECTSOUNDBUFFER)clip;
	buffer->Stop();
}


void setVolume(BufferHandle clip, float volume)
{
	auto buffer = (LPDIRECTSOUNDBUFFER)clip;
	buffer->SetVolume(DSBVOLUME_MIN + LONG(volume * (DSBVOLUME_MAX - DSBVOLUME_MIN)));
}


void setFrequency(BufferHandle clip, float frequency)
{
	auto buffer = (LPDIRECTSOUNDBUFFER)clip;
	buffer->SetFrequency(
		DSBFREQUENCY_MIN + DWORD(frequency * (DSBFREQUENCY_MAX - DSBFREQUENCY_MIN)));
}


void setCurrentTime(BufferHandle clip, float time_seconds)
{
	auto buffer = (LPDIRECTSOUNDBUFFER)clip;
	WAVEFORMATEX format;
	if (SUCCEEDED(buffer->GetFormat(&format, sizeof(format), nullptr)))
	{
		DWORD pos = DWORD(format.nAvgBytesPerSec * time_seconds);
		buffer->SetCurrentPosition(pos);
	}
}


void update(float)
{
	g_audio_device.listener->CommitDeferredSettings();
}


void setSourcePosition(BufferHandle clip, float x, float y, float z)
{
	TODO("cache QueryInterface");
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
	float up_z)
{
	g_audio_device.listener->SetOrientation(
		front_x, front_y, front_z, up_x, up_y, up_z, DS3D_DEFERRED);
}


void setListenerPosition(float x, float y, float z)
{
	g_audio_device.listener->SetPosition(x, y, z, DS3D_DEFERRED);
}


} // namespace Audio


} // namespace Lumix