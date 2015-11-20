#pragma once


namespace Lumix
{


class Engine;
class IAllocator;
class Path;


namespace Audio
{

	enum class ClipFlags
	{
		IS3D,

		COUNT
	};


	bool init(Engine& engine, IAllocator& allocator);
	void shutdown();

	typedef void* ClipHandle;
	ClipHandle load(const Path& path, const void* data, int size, int channels, int sample_rate, int flags);
	void unload(ClipHandle clip);
	void play(ClipHandle clip);
	void stop(ClipHandle clip);
	void pause(ClipHandle clip);
	void setVolume(ClipHandle clip, float volume);
	void setFrequency(ClipHandle clip, float frequency);
	void setCurrentPosition(ClipHandle clip, float time_seconds);
	void setListenerPosition(int index, float x, float y, float z);
	void setSourcePosition(ClipHandle clip, float x, float y, float z);

} // namespace Audio


} // namespace Lumix