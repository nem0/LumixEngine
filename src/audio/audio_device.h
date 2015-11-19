#pragma once


namespace Lumix
{


class Engine;
class IAllocator;
class Path;


namespace Audio
{


	bool init(Engine& engine, IAllocator& allocator);
	void shutdown();

	typedef void* ClipHandle;
	ClipHandle load(const Path& path);
	void unload(ClipHandle clip);
	void play(ClipHandle clip);
	void stop(ClipHandle clip);
	void pause(ClipHandle clip);
	void setVolume(ClipHandle clip, float volume);
	void setFrequency(ClipHandle clip, float frequency);
	void setCurrentPosition(ClipHandle clip, float time_seconds);


} // namespace Audio


} // namespace Lumix