#include "audio_device.h"
#include "core/path.h"
#include "engine/engine.h"
#include "engine/iplugin.h"
#define STB_VORBIS_HEADER_ONLY
#include "stb_vorbis.c"
#include <dsound.h>


namespace Lumix
{


namespace Audio
{


extern "C" LUMIX_LIBRARY_EXPORT IPlugin* createPlugin(Engine& engine)
{
	init(engine, engine.getAllocator());
	auto s = load(Lumix::Path("test.ogg"));
	play(s);
	return nullptr;
}


struct 
{
	HMODULE library;
	LPDIRECTSOUND direct_sound;
	bool initialized;
} g_audio_device;


static bool initPrimaryBuffer()
{
	DSBUFFERDESC primary_buffer_desc = {};
	primary_buffer_desc.dwSize = sizeof(primary_buffer_desc);
	primary_buffer_desc.dwFlags = DSBCAPS_PRIMARYBUFFER;
	LPDIRECTSOUNDBUFFER primary_buffer;
	auto result = SUCCEEDED(g_audio_device.direct_sound->CreateSoundBuffer(&primary_buffer_desc, &primary_buffer, nullptr));
	if (!result) return false;

	WAVEFORMATEX wave_format = {};
	wave_format.cbSize = 0;
	wave_format.nChannels = 2;
	wave_format.nSamplesPerSec = 24*1024;
	wave_format.wBitsPerSample = 16;
	wave_format.nBlockAlign = wave_format.nChannels * wave_format.wBitsPerSample / 8;
	wave_format.nAvgBytesPerSec = wave_format.nSamplesPerSec * wave_format.nBlockAlign;
	wave_format.wFormatTag = WAVE_FORMAT_PCM;

	result = SUCCEEDED(primary_buffer->SetFormat(&wave_format));
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
	if(!result)
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


ClipHandle load(const Path& path)
{
	int channels;
	int sample_rate;
	short* output;
	auto res = stb_vorbis_decode_filename(path.c_str(), &channels, &sample_rate, &output);

	DSBUFFERDESC testdesc = {};
	LPDIRECTSOUNDBUFFER buffer;
	testdesc.dwSize = sizeof(testdesc);
	testdesc.dwFlags = DSBCAPS_LOCSOFTWARE;
	testdesc.dwBufferBytes = res;

	WAVEFORMATEX wave_format = {};
	wave_format.cbSize = 0;
	wave_format.nChannels = channels;
	wave_format.nSamplesPerSec = sample_rate;
	wave_format.wBitsPerSample = 16;
	wave_format.nBlockAlign = wave_format.nChannels * wave_format.wBitsPerSample / 8;
	wave_format.nAvgBytesPerSec = wave_format.nSamplesPerSec * wave_format.nBlockAlign;
	wave_format.wFormatTag = WAVE_FORMAT_PCM;

	testdesc.lpwfxFormat = &wave_format;
	auto result = SUCCEEDED(g_audio_device.direct_sound->CreateSoundBuffer(&testdesc, &buffer, nullptr));

	void* p1;
	void* p2;
	DWORD s1, s2;
	result = SUCCEEDED(buffer->Lock(0, res, &p1, &s1, &p2, &s2, 0));
	memcpy(p1, output, s1);
	result = SUCCEEDED(buffer->Unlock(p1, s1, p2, s2));
	result = SUCCEEDED(buffer->SetCurrentPosition(0));
	free(output);

	return buffer;
}


void unload(ClipHandle clip)
{
	auto buffer = (LPDIRECTSOUNDBUFFER)clip;
	buffer->Release();
	ASSERT(false);
	TODO("todo");
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


} // namespace Audio


} // namespace Lumix