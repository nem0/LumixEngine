#pragma once


#include "lumix.h"


namespace Lumix
{


class Clip;
class Engine;
class IAllocator;
class Path;


namespace Audio
{

enum class BufferFlags
{
	IS3D = 1,
	LOOPED = 1 << 1
};

typedef void* BufferHandle;
static const BufferHandle INVALID_BUFFER_HANDLE = nullptr;

bool init(Engine& engine);
void shutdown();

LUMIX_AUDIO_API BufferHandle createBuffer(const void* data, int size_bytes, int channels, int sample_rate, int flags);
LUMIX_AUDIO_API void destroyBuffer(BufferHandle buffer);
LUMIX_AUDIO_API void play(BufferHandle buffer, bool looped);
LUMIX_AUDIO_API void stop(BufferHandle buffer);
LUMIX_AUDIO_API void pause(BufferHandle buffer);
LUMIX_AUDIO_API void setVolume(BufferHandle buffer, float volume);
LUMIX_AUDIO_API void setFrequency(BufferHandle buffer, float frequency);
LUMIX_AUDIO_API void setCurrentTime(BufferHandle buffer, float time_seconds);
LUMIX_AUDIO_API void setListenerPosition(float x, float y, float z);
LUMIX_AUDIO_API void setListenerOrientation(float front_x,
	float front_y,
	float front_z,
	float up_x,
	float up_y,
	float up_z);
LUMIX_AUDIO_API void setSourcePosition(BufferHandle buffer, float x, float y, float z);
LUMIX_AUDIO_API void update(float time_delta);

} // namespace Audio


} // namespace Lumix