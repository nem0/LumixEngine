#pragma once


#include "engine/lumix.h"


#ifdef STATIC_PLUGINS
	#define LUMIX_AUDIO_API
#elif defined BUILDING_AUDIO
	#define LUMIX_AUDIO_API LUMIX_LIBRARY_EXPORT
#else
	#define LUMIX_AUDIO_API LUMIX_LIBRARY_IMPORT
#endif


namespace Lumix
{


struct Clip;
struct DVec3;
struct Engine;
struct IAllocator;
struct Path;
template <typename T> struct UniquePtr;


struct LUMIX_AUDIO_API AudioDevice
{
	enum class BufferFlags {
		IS3D = 1,
	};

	static constexpr int MAX_PLAYING_SOUNDS = 256;

	using BufferHandle = i32;
	static constexpr BufferHandle INVALID_BUFFER_HANDLE = -1;

	virtual ~AudioDevice() {}

	static UniquePtr<AudioDevice> create(Engine& engine);

	virtual BufferHandle createBuffer(const void* data, int size_bytes, int channels, int sample_rate, int flags) = 0;
	virtual void setEcho(BufferHandle handle,
		float wet_dry_mix,
		float feedback,
		float left_delay,
		float right_delay) = 0;
	virtual void setChorus(BufferHandle handle,
		float wet_dry_mix,
		float depth,
		float feedback,
		float frequency,
		float delay,
		i32 phase) = 0;
	virtual void play(BufferHandle buffer, bool looped) = 0;
	virtual bool isPlaying(BufferHandle buffer) = 0;
	virtual bool isEnd(BufferHandle buffer) = 0;
	virtual void stop(BufferHandle buffer) = 0;
	virtual void pause(BufferHandle buffer) = 0;
	virtual void setMasterVolume(float volume) = 0;
	virtual void setVolume(BufferHandle buffer, float volume) = 0;
	virtual void setFrequency(BufferHandle buffer, u32 frequency_hz) = 0;
	virtual void setCurrentTime(BufferHandle buffer, float time_seconds) = 0;
	virtual float getCurrentTime(BufferHandle buffer) = 0;
	virtual void setListenerPosition(const DVec3& pos) = 0;
	virtual void setListenerOrientation(float front_x,
		float front_y,
		float front_z,
		float up_x,
		float up_y,
		float up_z) = 0;
	virtual void setSourcePosition(BufferHandle buffer, const DVec3& pos) = 0;
	virtual void update(float time_delta) = 0;
};


} // namespace Lumix