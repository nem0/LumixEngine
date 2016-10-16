#include "audio_device.h"
#include "clip_manager.h"
#include "engine/log.h"
#include "engine/engine.h"
#include "engine/iplugin.h"


namespace Lumix
{


class AudioDeviceImpl : public AudioDevice
{
public:
	BufferHandle createBuffer(const void* data,
		int size_bytes,
		int channels,
		int sample_rate,
		int flags) override
	{
		return INVALID_BUFFER_HANDLE;
	}
	void setEcho(BufferHandle handle,
		float wet_dry_mix,
		float feedback,
		float left_delay,
		float right_delay) override {}
	void play(BufferHandle buffer, bool looped) override {}
	bool isPlaying(BufferHandle buffer) override { return false; }
	void stop(BufferHandle buffer) override {}
	bool isEnd(BufferHandle buffer) override { return true; }
	void pause(BufferHandle buffer) override {}
	void setMasterVolume(float volume) override {}
	void setVolume(BufferHandle buffer, float volume) override {}
	void setFrequency(BufferHandle buffer, float frequency) override {}
	void setCurrentTime(BufferHandle buffer, float time_seconds) override {}
	float getCurrentTime(BufferHandle buffer) override { return -1; }
	void setListenerPosition(float x, float y, float z) override {}
	void setListenerOrientation(float front_x,
		float front_y,
		float front_z,
		float up_x,
		float up_y,
		float up_z) override {}
	void setSourcePosition(BufferHandle buffer, float x, float y, float z) override {}
	void update(float time_delta) override {}
};


static AudioDeviceImpl g_null_device;



AudioDevice* AudioDevice::create(Engine& engine)
{
	return &g_null_device;
}


void AudioDevice::destroy(AudioDevice& device)
{
}


} // namespace Lumix