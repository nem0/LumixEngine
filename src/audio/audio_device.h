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


} // namespace Audio


} // namespace Lumix