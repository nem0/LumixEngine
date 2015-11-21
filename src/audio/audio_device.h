#pragma once


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
	IS3D = 1
};

typedef void* BufferHandle;
static const BufferHandle INVALID_BUFFER_HANDLE = nullptr;

bool init(Engine& engine, IAllocator& allocator);
void shutdown();

BufferHandle createBuffer(const void* data, int size_bytes, int channels, int sample_rate, int flags);
void destroyBuffer(BufferHandle buffer);
void play(BufferHandle buffer);
void stop(BufferHandle buffer);
void pause(BufferHandle buffer);
void setVolume(BufferHandle buffer, float volume);
void setFrequency(BufferHandle buffer, float frequency);
void setCurrentTime(BufferHandle buffer, float time_seconds);
void setListenerPosition(int index, float x, float y, float z);
void setSourcePosition(BufferHandle buffer, float x, float y, float z);
void update(float time_delta);

} // namespace Audio


} // namespace Lumix