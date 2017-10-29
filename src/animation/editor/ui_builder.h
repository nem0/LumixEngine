#pragma once


namespace Lumix
{


template <typename T> auto getMembers();


// inspired by https://github.com/eliasdaler/MetaStuff
template <typename... Members>
struct ClassDesc
{
	const char* name;
	Tuple<Members...> members;
};


template <typename Class, typename T, typename... Attrs>
struct Member
{
	using RefSetter = void (Class::*)(const T&);
	using ValueSetter = void (Class::*)(T);
	using RefGetter = T& (Class::*)();
	using MemberPtr = T (Class::*);

	Member() {}
	Member(const char* name, MemberPtr member_ptr, Attrs... attrs);
	Member(const char* name, RefGetter getter, Attrs... attrs);
	Member(const char* name, RefGetter getter, RefSetter setter, Attrs... attrs);
	Member(const char* name, RefGetter getter, ValueSetter setter, Attrs... attrs);

	T& getRef(Class& obj) const;
	template <typename V> void set(Class& obj, const V& value) const;
	bool hasSetter() const { return ref_setter || value_setter; }

	const char* name = nullptr;
	bool has_member_ptr = false;
	MemberPtr member_ptr = nullptr;
	RefSetter ref_setter = nullptr;
	ValueSetter value_setter = nullptr;
	RefGetter ref_getter = nullptr;
	Tuple<Attrs...> attributes;
};


template <typename Class, typename T, typename... Attrs>
Member<Class, T, Attrs...>::Member(const char* name, MemberPtr member_ptr, Attrs... attrs)
	: name(name)
	, member_ptr(member_ptr)
	, has_member_ptr(true)
{
	attributes = makeTuple(attrs...);
}


template <typename Class, typename T, typename... Attrs>
Member<Class, T, Attrs...>::Member(const char* name, RefGetter getter, RefSetter setter, Attrs... attrs)
	: name(name)
	, ref_getter(getter)
	, ref_setter(setter)
{
	attributes = makeTuple(attrs...);
}


template <typename Class, typename T, typename... Attrs>
Member<Class, T, Attrs...>::Member(const char* name, RefGetter getter, Attrs... attrs)
	: name(name)
	, ref_getter(getter)
{
	attributes = makeTuple(attrs...);
}


template <typename Class, typename T, typename... Attrs>
Member<Class, T, Attrs...>::Member(const char* name, RefGetter getter, ValueSetter setter, Attrs... attrs)
	: name(name)
	, ref_getter(getter)
	, value_setter(setter)
{
	attributes = makeTuple(attrs...);
}


template <typename Class, typename T, typename... Attrs>
template <typename V> 
void Member<Class, T, Attrs...>::set(Class& obj, const V& value) const
{
	if (ref_setter)
	{
		(obj.*ref_setter)(value);
	}
	else if (value_setter)
	{
		(obj.*value_setter)(value);
	}
	else if (has_member_ptr)
	{
		(obj.*member_ptr) = value;
	}
	else
	{
		ASSERT(false);
	}
}


template <typename Class, typename T, typename... Attrs>
T& Member<Class, T, Attrs...>::getRef(Class& obj) const
{
	if (ref_getter) return (obj.*ref_getter)();
	if (has_member_ptr) return obj.*member_ptr;
	ASSERT(false);
	return obj.*member_ptr;
}


template <typename... Members>
auto type(const char* name, Members... members)
{
	ClassDesc<Members...> class_desc;
	class_desc.name = name;
	class_desc.members = makeTuple(members...);
	return class_desc;
}


template <typename T, typename Class, typename... Attrs>
auto property(const char* name, T (Class::*getter)(), Attrs... attrs)
{
	return Member<Class, RemoveCVR<T>, Attrs...>(name, getter, attrs...);
}


template <typename T, typename Class, typename... Attrs>
auto property(const char* name, T (Class::*member), Attrs... attrs)
{
	return Member<Class, RemoveCVR<T>, Attrs...>(name, member, attrs...);
}


template <typename R, typename Class, typename T, typename... Attrs>
auto property(const char* name, R& (Class::*getter)(), void (Class::*setter)(T), Attrs... attrs)
{
	return Member<Class, RemoveCVR<R>, Attrs...>(name, getter, setter, attrs...);
}


template <typename T>
struct EnumValue
{
	T value;
	const char* name;
};


template <typename T> auto getEnum() { return makeTuple(); }
template <typename T> 
int getEnumValueIndex(T value)
{
	int out = 0;
	int idx = 0;
	auto enum_desc = getEnum<T>();
	apply([&out, &idx, value](const auto& enum_value) {
		if (enum_value.value == value) out = idx;
		++idx;
	}, enum_desc);
	return out;
}
template <typename T> 
T getEnumValueFromIndex(int idx)
{
	T out;
	auto enum_desc = getEnum<T>();
	apply([&idx, &out](const auto& enum_value) {
		if (idx == 0) out = enum_value.value;
		--idx;
	}, enum_desc);
	return out;
}

template <typename Counter>
struct ArraySizeAttribute
{
	Counter counter;
};


template <typename Adder, typename Remover>
struct ArrayAttribute
{
	Adder adder;
	Remover remover;
};


template <typename F>
struct CustomUIAttribute {};


template <typename Adder, typename Remover>
auto array_attribute(Adder adder, Remover remover)
{
	ArrayAttribute<Adder, Remover> attr;
	attr.adder = adder;
	attr.remover = remover;
	return attr;
}


template <typename Counter>
auto array_size_attribute(Counter counter)
{
	ArraySizeAttribute<Counter> attr;
	attr.counter = counter;
	return attr;
}


inline void serialize(OutputBlob& blob, string& obj)
{
	blob.write(obj);
}


inline void serialize(OutputBlob& blob, int obj)
{
	blob.write(obj);
}


template <int count>
void serialize(OutputBlob& blob, StaticString<count>& obj)
{
	blob.writeString(obj.data);
}


template <typename T>
void serialize(OutputBlob& blob, Array<T>& array)
{
	blob.write(array.size());
	for (T& item : array)
	{
		serialize(blob, item);
		blob.write(item);
	}
}


template <typename T>
typename EnableIf<TupleSize<decltype(getEnum<T>())>::result == 0>::Type
serialize(OutputBlob& blob, T& obj)
{
	auto desc = getMembers<RemoveCVR<T>>();
	auto l = [&blob, &obj](const auto& member) {
		decltype(auto) x = member.getRef(obj);
		serialize(blob, x);
	};
	apply(l, desc.members);
}


template <typename T>
typename EnableIf<TupleSize<decltype(getEnum<T>())>::result != 0>::Type
serialize(OutputBlob& blob, T& obj)
{
	blob.write(obj);
}


template <typename Class>
void deserialize(InputBlob& blob, Class& obj)
{
	auto desc = getMembers<Class>();
	auto l = [&blob, &obj](const auto& member) {
		auto& value = member.getRef(obj);
		deserialize(blob, obj, member, value);
	};
	apply(l, desc.members);
}


template <typename Parent, typename Member, typename Class>
typename EnableIf<TupleSize<decltype(getEnum<Class>())>::result == 0>::Type
deserialize(InputBlob& blob, Parent& parent, const Member& member, Class& obj)
{
	ASSERT(!member.hasSetter());
	auto l = [&blob, &obj](const auto& member) {
		auto& value = member.getRef(obj);
		deserialize(blob, obj, member, value);
	};
	apply(l, getMembers<Class>().members);
}


template <typename Parent, typename Member, typename Class>
typename EnableIf<TupleSize<decltype(getEnum<Class>())>::result != 0>::Type
deserialize(InputBlob& blob, Parent& parent, const Member& member, Class& obj)
{
	Class tmp;
	blob.read(tmp);
	member.set(parent, tmp);
}


template <typename Parent, typename Member>
void deserialize(InputBlob& blob, Parent& parent, const Member& member, string& obj)
{
	ASSERT(!member.hasSetter());
	blob.read(obj);
}

template <typename Parent, typename Member>
void deserialize(InputBlob& blob, Parent& parent, const Member& member, int& obj)
{
	member.set(parent, obj);
}

template <typename Parent, typename Member, int count>
void deserialize(InputBlob& blob, Parent& parent, const Member& member, StaticString<count>& obj)
{
	StaticString<count> tmp;
	blob.readString(tmp.data, lengthOf(tmp.data));
	member.set(parent, tmp);
}


template <typename Parent, typename Member, typename T>
void deserialize(InputBlob& blob, Parent& parent, const Member& member, Array<T>& array)
{
	ASSERT(!member.hasSetter());
	int count = blob.read<int>();
	for (int i = 0; i < count; ++i)
	{
		addToArray(member, parent, -1);
		deserialize(blob, array[i]);
	}
}


template <typename F>
struct ArrayAttributeVisitor
{
	ArrayAttributeVisitor(F f)
		: f(f)
	{}

	template <typename Adder, typename Remover>
	void operator()(const ArrayAttribute<Adder, Remover>& attr) const
	{
		f(attr);
	}

	template <typename T>
	void operator()(const T& x) const {}

	F f;
};


template <typename F>
struct ArraySizeAttributeVisitor
{
	ArraySizeAttributeVisitor(F f)
		: f(f)
	{}

	template <typename Counter>
	void operator()(const ArraySizeAttribute<Counter>& attr) const
	{
		f(attr);
	}

	template <typename T>
	void operator()(const T& x) const {}

	F f;
};


template <typename T>
int getArraySize(const Array<T>& array)
{
	return array.size();
}


template <typename T, int count>
int getArraySize(const T (&array)[count])
{
	return count;
}


template <typename Member, typename Parent>
int getArraySize(const Member& member, Parent& parent)
{
	auto& array = member.getRef(parent);
	int size = getArraySize(array);
	auto lambda = [&size, &parent](const auto& attr) {
		size = (parent.*attr.counter)();
	};
	ArraySizeAttributeVisitor<decltype(lambda)> visitor(lambda);
	apply(visitor, member.attributes);
	return size;
}


template <typename Member, typename Parent>
void removeFromArray(const Member& member, Parent& parent, int index)
{
	auto lambda = [index, &parent](const auto& attr) {
		(parent.*attr.remover)(index);
	};
	ArrayAttributeVisitor<decltype(lambda)> visitor(lambda);
	apply(visitor, member.attributes);
}


template <typename Member, typename Parent>
void addToArray(const Member& member, Parent& parent, int index)
{
	auto lambda = [index, &parent](const auto& attr) {
		(parent.*attr.adder)(index);
	};
	ArrayAttributeVisitor<decltype(lambda)> visitor(lambda);
	apply(visitor, member.attributes);
}



struct IHashedCommand : IEditorCommand
{
	virtual u32 getTypeHash() = 0;
};

template <typename Root, typename T, typename PP>
struct SetPropertyCommand : IHashedCommand
{
	SetPropertyCommand(Root& root, PP pp, const T& value)
		: root(root)
		, pp(pp)
		, value(value)
		, old_value(value)
	{}


	bool execute() override
	{
		old_value = pp.getValueFromRoot(root);
		pp.setValueFromRoot(root, value);
		return true;
	}


	void undo() override
	{
		pp.setValueFromRoot(root, old_value);
	}


	u32 getTypeHash() override
	{
		Root* root_ptr = &root;
		u32 hash = crc32(&root_ptr, sizeof(root_ptr));
		hash = continueCrc32(hash, &pp, sizeof(pp));
		return hash;
	}


	void serialize(JsonSerializer& serializer) override { ASSERT(false); }
	void deserialize(JsonSerializer& serializer) override { ASSERT(false); }
	const char* getType() override { return "set_anim_editor_property"; }
	
	
	bool merge(IEditorCommand& command) override
	{ 
		if (command.getType() != getType()) return false;
		if (getTypeHash() != ((IHashedCommand&)command).getTypeHash()) return false;
		((SetPropertyCommand<Root, T, PP>&)command).value = value;
		return true; 
	}


	T value;
	T old_value;
	PP pp;
	Root& root;
};


template <typename Root, typename PP>
struct RemoveArrayItemCommand : IEditorCommand
{
	RemoveArrayItemCommand(Root& root, PP _pp, int _index, IAllocator& allocator)
		: root(root)
		, pp(_pp)
		, index(_index)
		, blob(allocator)
	{}


	bool execute() override
	{
		auto& owner = ((typename PP::Base&)pp).getValueFromRoot(root);
		auto& array = pp.getValueFromRoot(root);
		::Lumix::serialize(blob, array[index]);
		removeFromArray(pp.head, owner, index);
		return true;
	}


	void undo() override
	{
		auto& owner = ((typename PP::Base&)pp).getValueFromRoot(root);
		addToArray(pp.head, owner, index);
		InputBlob input_blob(blob);
		auto& array = pp.getValueFromRoot(root);
		::Lumix::deserialize(input_blob, array[index]);
	}


	void serialize(JsonSerializer& serializer) override { ASSERT(false); }
	void deserialize(JsonSerializer& serializer) override { ASSERT(false); }
	const char* getType() override { return "remove_array_item_anim_editor_property"; }
	bool merge(IEditorCommand& command) override { return false; }

	OutputBlob blob;
	PP pp;
	int index;
	Root& root;
};


template <typename Root, typename PropertyPath>
struct AddArrayItemCommand : IEditorCommand
{
	AddArrayItemCommand(Root& root, PropertyPath _pp)
		: root(root)
		, pp(_pp)
	{}


	bool execute() override
	{
		auto& owner = ((typename PropertyPath::Base&)pp).getValueFromRoot(root);
		addToArray(pp.head, owner, -1);
		return true;
	}


	void undo() override
	{
		auto& owner = ((typename PropertyPath::Base&)pp).getValueFromRoot(root);
		auto& array = pp.getValueFromRoot(root);
		int size = getArraySize(pp.head, owner);
		removeFromArray(pp.head, owner, size - 1);
	}


	void serialize(JsonSerializer& serializer) override { ASSERT(false); }
	void deserialize(JsonSerializer& serializer) override { ASSERT(false); }
	const char* getType() override { return "add_array_item_anim_editor_property"; }
	bool merge(IEditorCommand& command) override { return false; }

	PropertyPath pp;
	Root& root;
};


struct PropertyPathBegin
{
	template <typename T>
	decltype(auto) getValueFromRoot(T& root) const { return root; }
};


template <typename Prev, typename Member>
struct PropertyPath : Prev
{
	using Base = Prev;

	PropertyPath(const Prev& prev, Member _head) : Prev(prev), head(_head), name(_head.name) {}

	template <typename T>
	decltype(auto) getValue(T& obj) const { return head.getRef(obj); }

	template <typename T>
	decltype(auto) getValueFromRoot(T& root) const
	{
		return head.getRef(Prev::getValueFromRoot(root));
	}

	template <typename T, typename O>
	void setValueFromRoot(T& root, O value)
	{
		decltype(auto) x = Prev::getValueFromRoot(root);
		head.set(x, value);
	}

	Member head;
	const char* name;
};


template <typename Prev>
struct PropertyPathArray : Prev
{
	using Base = Prev;

	PropertyPathArray(const Prev& prev, int _index)
		: Prev(prev), index(_index) {}

	template <typename T>
	auto& getValue(T& obj) const { return obj[index]; }

	template <typename T>
	auto& getValueFromRoot(T& root) const
	{
		return Prev::getValueFromRoot(root)[index];
	}


	template <typename T, typename O>
	void setValueFromRoot(T& root, const O& value)
	{
		Prev::getValueFromRoot(root)[index] = value;
	}

	int index;
};


template <typename Prev, typename Member>
auto makePP(const Prev& prev, const Member& head)
{
	return PropertyPath<typename RemoveReference<Prev>::Type, typename RemoveReference<Member>::Type>(prev, head);
}


template <typename Prev>
auto makePP(const Prev& prev, int index)
{
	return PropertyPathArray<Prev>(prev, index);
}


template <typename Editor, typename Root>
struct UIBuilder
{
	UIBuilder(Editor& editor, Root& root, IAllocator& allocator) 
		: m_editor(editor)
		, m_root(root)
		, m_allocator(allocator)
	{}


	void build()
	{
		auto desc = getMembers<Root>();
		apply([this](const auto& member) {
			auto pp = makePP(PropertyPathBegin{}, member);
			auto& v = member.getRef(m_root);
			this->ui(m_root, pp, v);
		}, desc.members);
	}


	template <typename O, typename PP, typename T>
	struct CustomUIVisitor
	{
		CustomUIVisitor(O& owner, const PP& pp, T& obj)
			: owner(owner)
			, pp(pp)
			, obj(obj)
		{}

		template <typename Attr>
		void operator()(const Attr& attr) {}
		
		template <typename F>
		void operator()(const CustomUIAttribute<F>& attr)
		{
			F::build(owner, pp, obj);
			has_custom_ui = true;
		}

		O& owner;
		const PP& pp;
		T& obj;
		bool has_custom_ui = false;
	};


	template <typename O, typename PP, typename T>
	bool customUI(O& owner, const PP& pp, T& obj)
	{
		CustomUIVisitor<O, PP, T> visitor(owner, pp, obj);
		apply(visitor, pp.head.attributes);
		return visitor.has_custom_ui;
	}


	template <typename O, typename PP, typename T, int capacity>
	void ui(O& owner, const PP& pp, T (&array)[capacity])
	{
		if (customUI(owner, pp, array)) return;

		bool expanded = ImGui::TreeNodeEx(pp.name, ImGuiTreeNodeFlags_AllowOverlapMode);
		ImGui::SameLine();
		if (ImGui::SmallButton(StaticString<32>("Add")))
		{
			auto* command = LUMIX_NEW(m_allocator, AddArrayItemCommand<Root, PP>)(m_root, pp);
			m_editor.executeCommand(*command);
		}
		if (!expanded) return;

		int i = 0;
		int subproperties_count = TupleSize<decltype(getMembers<T>().members)>::result;
		if (subproperties_count > 1)
		{
			ASSERT(false);
			// TODO
/*			for (T& item : array)
			{
				StaticString<32> label("", i + 1);
				bool expanded = ImGui::TreeNodeEx(label, ImGuiTreeNodeFlags_AllowOverlapMode);
				ImGui::SameLine();
				if (ImGui::SmallButton("Remove"))
				{
					auto* command = LUMIX_NEW(m_allocator, RemoveArrayItemCommand<Root, PP>)(m_root, pp, i);
					m_editor.executeCommand(*command);
					if (expanded) ImGui::TreePop();
					break;
				}

				if (expanded)
				{
					ui(array, makePP(pp, i), item);
					ImGui::TreePop();
				}
				++i;
			}*/
		}
		else
		{
			for (int i = 0, c = getArraySize(pp.head, owner); i < c; ++i)
			{
				T& item = array[i];
				ImGui::PushID(&item);
				ui(array, makePP(pp, i), item);
				ImGui::SameLine();
				if (ImGui::SmallButton("Remove"))
				{
					auto* command = LUMIX_NEW(m_allocator, RemoveArrayItemCommand<Root, PP>)(m_root, pp, i, m_allocator);
					m_editor.executeCommand(*command);
					ImGui::PopID();
					break;
				}
				ImGui::PopID();
			}
		}
		ImGui::TreePop();
	}


	template <typename O, typename PP, int count>
	void ui(O& owner, const PP& pp, StaticString<count>& obj)
	{
		if (customUI(owner, pp, obj)) return;

		if (ImGui::InputText(pp.name, obj.data, sizeof(obj.data)))
		{
			auto* command = LUMIX_NEW(m_allocator, SetPropertyCommand<Root, StaticString<count>, PP>)(m_root, pp, obj);
			m_editor.executeCommand(*command);
		}
	}


	template <typename O, typename PP>
	void ui(O& owner, const PP& pp, int& obj)
	{
		if (customUI(owner, pp, obj)) return;

		if (ImGui::InputInt(pp.name, &obj))
		{
			auto* command = LUMIX_NEW(m_allocator, SetPropertyCommand<Root, int, PP>)(m_root, pp, obj);
			m_editor.executeCommand(*command);
		}
	}


	template <typename O, typename PP, typename T>
	typename EnableIf<TupleSize<decltype(getEnum<T>())>::result == 0>::Type
		ui(O& owner, const PP& pp, T& obj)
	{
		if (customUI(owner, pp, obj)) return;

		auto desc = getMembers<T>();
		apply([&pp, this, &obj](const auto& m) {
			auto& v = m.getRef(obj);
			auto child_pp = makePP(pp, m);
			this->ui(obj, child_pp, v);
		}, desc.members);
	}


	template <typename O, typename PP, typename T>
	typename EnableIf<TupleSize<decltype(getEnum<T>())>::result != 0>::Type
		ui(O& owner, const PP& pp, T& obj)
	{
		auto enum_values = getEnum<T>();
		int idx = getEnumValueIndex(obj);
		auto getter = [](void* data, int index, const char** out){
			auto enum_values = getEnum<T>();
			apply([&out, &index](auto& enum_value) {
				if (index == 0) *out = enum_value.name;
				--index;
			}, enum_values);
			return true;
		};
		if (ImGui::Combo(pp.name, &idx, getter, nullptr, TupleSize<decltype(enum_values)>::result))
		{
			T val = getEnumValueFromIndex<T>(idx);
			pp.head.set(owner, val);
		}
	}


	template <typename O, typename PP>
	void ui(O& owner, const PP& pp, string& value)
	{
		if (customUI(owner, pp, value)) return;

		char tmp[32];
		copyString(tmp, value.c_str());
		if (ImGui::InputText(pp.name, tmp, sizeof(tmp)))
		{
			string tmp_str(tmp, m_allocator);
			auto* command = LUMIX_NEW(m_allocator, SetPropertyCommand<Root, string, PP>)(m_root, pp, tmp_str);
			m_editor.executeCommand(*command);
		}
	}


	template <typename Owner, typename PP, typename T>
	void ui(Owner& owner, const PP& pp, Array<T>& array)
	{
		if (customUI(owner, pp, array)) return;
		
		bool expanded = ImGui::TreeNodeEx(pp.name, ImGuiTreeNodeFlags_AllowOverlapMode | ImGuiTreeNodeFlags_Framed);
		ImGui::SameLine();
		if (ImGui::SmallButton("Add"))
		{
			auto* command = LUMIX_NEW(m_allocator, AddArrayItemCommand<Root, PP>)(m_root, pp);
			m_editor.executeCommand(*command);
		}
		if (!expanded) return;

		int i = 0;
		int subproperties_count = TupleSize<decltype(getMembers<T>().members)>::result;
		if (subproperties_count > 1)
		{
			for (T& item : array)
			{
				StaticString<32> label("", i + 1);
				bool expanded = ImGui::TreeNodeEx(label, ImGuiTreeNodeFlags_AllowOverlapMode);
				ImGui::SameLine();
				if (ImGui::SmallButton("Remove"))
				{
					auto* command = LUMIX_NEW(m_allocator, RemoveArrayItemCommand<Root, PP>)(m_root, pp, i, m_allocator);
					m_editor.executeCommand(*command);
					if (expanded) ImGui::TreePop();
					break;
				}

				if (expanded)
				{
					ui(array, makePP(pp, i), item);
					ImGui::TreePop();
				}
				++i;
			}
		}
		else
		{
			for (T& item : array)
			{
				ImGui::PushID(&item);
				ui(array, makePP(pp, i), item);
				ImGui::SameLine();
				if (ImGui::SmallButton("Remove"))
				{
					auto* command = LUMIX_NEW(m_allocator, RemoveArrayItemCommand<Root, PP>)(m_root, pp, i, m_allocator);
					m_editor.executeCommand(*command);
					ImGui::PopID();
					break;
				}
				ImGui::PopID();
				++i;
			}
		}
		ImGui::TreePop();
	}

	IAllocator& m_allocator;
	Editor& m_editor;
	Root& m_root;
};


} // namespace Lumix