#pragma once


#include "engine/iplugin.h"


namespace Lumix
{


class AudioSystem;
class Clip;
class Path;


class AudioScene : public IScene
{
public:
	typedef int SoundHandle;
	static const SoundHandle INVALID_SOUND_HANDLE = -1;

public:
	static AudioScene* createInstance(AudioSystem& system,
		Engine& engine,
		Universe& universe,
		class IAllocator& allocator);
	static void destroyInstance(AudioScene* scene);

	virtual int getClipCount() const = 0;
	virtual int addClip(const char* name, const Lumix::Path& path) = 0;
	virtual void removeClip(int clip_id) = 0;
	virtual int getClipID(const char* name) = 0;
	virtual bool isClipIDValid(int clip_id) = 0;
	virtual Clip* getClip(int clid_id) = 0;
	virtual void setClip(int clip_id, const Lumix::Path& path) = 0;
	virtual const char* getClipName(int clip_id) = 0;
	virtual void setClipName(int clip_id, const char* clip_name) = 0;

	virtual SoundHandle play(Entity entity, int clip_id) = 0;
	virtual void stop(SoundHandle sound_id) = 0;
	virtual void setVolume(SoundHandle sound_id, float volume) = 0;
};


} // namespace Lumix