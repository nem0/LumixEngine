#pragma once


#include "lumix.h"


namespace Lumix
{


class Clip;
class Engine;
class IAllocator;
class Path;


class LUMIX_AUDIO_API AudioDevice
{
public:
	enum class BufferFlags
	{
		IS3D = 1,
		LOOPED = 1 << 1
	};

	static const int MAX_PLAYING_SOUNDS = 256;

	typedef int BufferHandle;
	static const BufferHandle INVALID_BUFFER_HANDLE;

public:
	virtual ~AudioDevice() {}

	static AudioDevice* create(Engine& engine);
	static void destroy(AudioDevice& device);

	virtual BufferHandle createBuffer(const void* data, int size_bytes, int channels, int sample_rate, int flags) = 0;
	virtual void setEcho(BufferHandle handle,
		float wet_dry_mix,
		float feedback,
		float left_delay,
		float right_delay) = 0;
	virtual void play(BufferHandle buffer, bool looped) = 0;
	virtual bool isPlaying(BufferHandle buffer) = 0;
	virtual void stop(BufferHandle buffer) = 0;
	virtual void pause(BufferHandle buffer) = 0;
	virtual void setVolume(BufferHandle buffer, float volume) = 0;
	virtual void setFrequency(BufferHandle buffer, float frequency) = 0;
	virtual void setCurrentTime(BufferHandle buffer, float time_seconds) = 0;
	virtual float getCurrentTime(BufferHandle buffer) = 0;
	virtual void setListenerPosition(float x, float y, float z) = 0;
	virtual void setListenerOrientation(float front_x,
		float front_y,
		float front_z,
		float up_x,
		float up_y,
		float up_z) = 0;
	virtual void setSourcePosition(BufferHandle buffer, float x, float y, float z) = 0;
	virtual void update(float time_delta) = 0;
};


} // namespace Lumix