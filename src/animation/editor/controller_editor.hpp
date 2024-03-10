#pragma once

#include "editor/studio_app.hpp"

namespace Lumix {

template <typename T> struct UniquePtr;

namespace anim_editor {

struct Node;

enum class ControllerVersion : u32 {
	FIRST_SUPPORTED = 4,

	LATEST
};

struct Controller final {
	Controller(const Path& path, IAllocator& allocator);
	~Controller();

	void serialize(OutputMemoryStream& stream);
	bool deserialize(InputMemoryStream& stream);
	void clear();
	bool compile(StudioApp& app, OutputMemoryStream& blob);

	struct AnimationEntry {
		u32 set;
		u32 slot;
		Path animation;
	};

	struct Input {
		anim::Value::Type type;
		StaticString<32> name;
	};

	Path m_path;
	IAllocator& m_allocator;
	struct TreeNode* m_root = nullptr;
	Array<AnimationEntry> m_animation_entries;
	Array<String> m_animation_slots;
	Array<BoneMask> m_bone_masks;
	Array<Input> m_inputs;
	u32 m_id_generator = 0;
	Path m_skeleton;
	bool m_compiled = false;
};

struct ControllerEditor {
	static UniquePtr<ControllerEditor> create(StudioApp& app);
	virtual ~ControllerEditor() {}
};

} // namespace anim
} // namespace Lumix