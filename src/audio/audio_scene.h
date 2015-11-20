#pragma once


#include "engine/iplugin.h"


namespace Lumix
{


class Path;


class AudioScene : public IScene
{
public:
	typedef int SoundHandle;

public:
	static AudioScene* createInstance(IPlugin& system,
		Engine& engine,
		Universe& universe,
		class IAllocator& allocator);
	static void destroyInstance(AudioScene* scene);

	virtual int getClipCount() const = 0;
	virtual int addClip(const char* name, const Lumix::Path& path) = 0;
	virtual void removeClip(int clip_id) = 0;
	virtual int getClipID(const char* name) = 0;

	virtual int play(Entity entity, int clip_id) = 0;
	virtual void stop(int sound_id) = 0;
	virtual void setVolume(int sound_id, float volume) = 0;
};


} // namespace Lumix