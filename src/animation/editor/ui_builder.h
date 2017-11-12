#pragma once


#include "editor/ieditor_command.h"
#include "engine/metaprogramming.h"


namespace Lumix
{


// inspired by https://github.com/eliasdaler/MetaStuff
template <typename... Members>
struct ClassDesc
{
	const char* name;
	Tuple<Members...> members;
};


template <typename T> auto getMembers()
{
	ClassDesc<> cd;
	cd.name = "not defined";
	ASSERT(false);
	return cd;
}


template <typename Class, typename T, typename... Attrs>
struct Member
{
	using RefSetter = void (Class::*)(const T&);
	using ValueSetter = void (Class::*)(T);
	using ConstRefGetter = const T& (Class::*)() const;
	using RefGetter = T& (Class::*)();
	using ValueGetter = T (Class::*)() const;
	using MemberPtr = T (Class::*);

	Member() {}
	Member(const char* name, MemberPtr member_ptr, Attrs... attrs);
	Member(const char* name, RefGetter getter, Attrs... attrs);
	Member(const char* name, ConstRefGetter getter, RefSetter setter, Attrs... attrs);
	Member(const char* name, ValueGetter getter, ValueSetter setter, Attrs... attrs);
	Member(const char* name, RefGetter getter, RefSetter setter, Attrs... attrs);
	Member(const char* name, RefGetter getter, ValueSetter setter, Attrs... attrs);

	Member& addConstRefGetter(ConstRefGetter getter) { const_ref_getter = getter; return *this; }

	template <typename F> void callWithValue(Class& obj, F f) const;
	template <typename F> void callWithValue(const Class& obj, F f) const;
	T getValue(const Class& obj) const { return (obj.*value_getter)(); }
	T& getRef(Class& obj) const;
	template <typename V> void set(Class& obj, const V& value) const;
	bool hasSetter() const { return ref_setter || value_setter; }

	const char* name = nullptr;
	bool has_member_ptr = false;
	MemberPtr member_ptr = nullptr;
	RefSetter ref_setter = nullptr;
	ValueSetter value_setter = nullptr;
	RefGetter ref_getter = nullptr;
	ConstRefGetter const_ref_getter = nullptr;
	ValueGetter value_getter = nullptr;
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
Member<Class, T, Attrs...>::Member(const char* name, ValueGetter getter, ValueSetter setter, Attrs... attrs)
	: name(name)
	, value_getter(getter)
	, value_setter(setter)
{
	attributes = makeTuple(attrs...);
}

template <typename Class, typename T, typename... Attrs>
Member<Class, T, Attrs...>::Member(const char* name, ConstRefGetter getter, RefSetter setter, Attrs... attrs)
	: name(name)
	, const_ref_getter(getter)
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


template <typename Class, typename T, typename... Attrs>
template <typename F>
void Member<Class, T, Attrs...>::callWithValue(Class& obj, F f) const
{
	if (const_ref_getter) return f((obj.*const_ref_getter)());
	if (ref_getter) return f((obj.*ref_getter)());
	if (has_member_ptr) return f(obj.*member_ptr);
	if (value_getter) return f((obj.*value_getter)());
	ASSERT(false);
}


template <typename Class, typename T, typename... Attrs>
template <typename F>
void Member<Class, T, Attrs...>::callWithValue(const Class& obj, F f) const
{
	if (const_ref_getter) return f((obj.*const_ref_getter)());
	if (has_member_ptr) return f(obj.*member_ptr);
	if (value_getter) return f((obj.*value_getter)());
	ASSERT(false);
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
auto property(const char* name, T& (Class::*getter)(), Attrs... attrs)
{
	return Member<Class, RemoveCVR<T>, Attrs...>(name, getter, attrs...);
}


template <typename T, typename Class, typename... Attrs>
auto property(const char* name, T (Class::*member), Attrs... attrs)
{
	return Member<Class, RemoveCVR<T>, Attrs...>(name, member, attrs...);
}


template <typename R, typename Class, typename T, typename... Attrs>
auto property(const char* name, R& (Class::*getter)() const, void (Class::*setter)(T), Attrs... attrs)
{
	return Member<Class, RemoveCVR<R>, Attrs...>(name, getter, setter, attrs...);
}


template <typename R, typename Class, typename T, typename... Attrs>
auto property(const char* name, R (Class::*getter)() const, void (Class::*setter)(T), Attrs... attrs)
{
	return Member<Class, RemoveCVR<R>, Attrs...>(name, getter, setter, attrs...);
}


template <typename R, typename Class, typename T, typename... Attrs>
auto property(const char* name, const R& (Class::*getter)() const, void (Class::*setter)(T), Attrs... attrs)
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


inline void serialize(OutputBlob& blob, const string& obj)
{
	blob.write(obj);
}


inline void serialize(OutputBlob& blob, int obj)
{
	blob.write(obj);
}


inline void serialize(OutputBlob& blob, u32 obj)
{
	blob.write(obj);
}


template <int count>
void serialize(OutputBlob& blob, const StaticString<count>& obj)
{
	blob.writeString(obj.data);
}


inline void serialize(OutputBlob& blob, const Path& obj)
{
	blob.writeString(obj.c_str());
}


template <typename T>
void serialize(OutputBlob& blob, const Array<T>& array)
{
	blob.write(array.size());
	for (T& item : array)
	{
		serialize(blob, item);
	}
}


template <typename T>
auto serializeImpl(OutputBlob& blob, const T& obj, int)
-> decltype(obj.serialize(blob), void())
{
	obj.serialize(blob);
}


template<class T>
void serializeImpl(OutputBlob& blob, const T& obj, long)
{
	auto desc = getMembers<RemoveCVR<T>>();
	auto l = [&blob, &obj](const auto& member) {
		member.callWithValue(obj, [&](const auto& x) {
			serialize(blob, x);
		});
	};
	apply(l, desc.members);
}


template <typename T>
typename EnableIf<TupleSize<decltype(getEnum<T>())>::result == 0>::Type
serialize(OutputBlob& blob, const T& obj)
{
	serializeImpl(blob, obj, 0);
}


template <typename T>
typename EnableIf<TupleSize<decltype(getEnum<T>())>::result != 0>::Type
serialize(OutputBlob& blob, const T& obj)
{
	blob.write(obj);
}


template <typename T>
auto deserializeImpl(InputBlob& blob, T& obj, int)
-> decltype(obj.deserialize(blob), void())
{
	obj.deserialize(blob);
}


template<class T>
void deserializeImpl(InputBlob& blob, T& obj, long)
{
	auto desc = getMembers<T>();
	auto l = [&blob, &obj](const auto& member) {
		member.callWithValue(obj, [&](const auto& value){
			deserialize(blob, obj, member, value);
		});

	};
	apply(l, desc.members);
}


template <typename Class>
void deserialize(InputBlob& blob, Class& obj)
{
	deserializeImpl(blob, obj, 0);
}


template <typename Parent, typename Member, typename Class>
typename EnableIf<TupleSize<decltype(getEnum<Class>())>::result == 0>::Type
deserialize(InputBlob& blob, Parent& parent, const Member& member, const Class& obj)
{
	ASSERT(!member.hasSetter());
	auto desc = getMembers<Class>();
	auto l = [&blob, &obj](const auto& member) {
		auto& value = member.getRef(obj);
		deserialize(blob, obj, member, value);
	};
	apply(l, desc.members);
}


template <typename Parent, typename Member, typename Class>
typename EnableIf<TupleSize<decltype(getEnum<Class>())>::result != 0>::Type
deserialize(InputBlob& blob, Parent& parent, const Member& member, const Class& obj)
{
	Class tmp;
	blob.read(tmp);
	member.set(parent, tmp);
}


template <typename Parent, typename Member>
void deserialize(InputBlob& blob, Parent& parent, const Member& member, const string& obj)
{
	string tmp(obj.m_allocator);
	blob.read(tmp);
	member.set(parent, tmp);
}

template <typename Parent, typename Member>
void deserialize(InputBlob& blob, Parent& parent, const Member& member, const Path& obj)
{
	char tmp[MAX_PATH_LENGTH];
	blob.readString(tmp, lengthOf(tmp));
	member.set(parent, Path(tmp));
}

template <typename Parent, typename Member>
void deserialize(InputBlob& blob, Parent& parent, const Member& member, int obj)
{
	int tmp;
	blob.read(tmp);
	member.set(parent, tmp);
}

template <typename Parent, typename Member>
void deserialize(InputBlob& blob, Parent& parent, const Member& member, u32 obj)
{
	u32 tmp;
	blob.read(tmp);
	member.set(parent, tmp);
}

template <typename Parent, typename Member, int count>
void deserialize(InputBlob& blob, Parent& parent, const Member& member, const StaticString<count>& obj)
{
	StaticString<count> tmp;
	blob.readString(tmp.data, lengthOf(tmp.data));
	member.set(parent, tmp);
}


template <typename Parent, typename Member, typename T>
void deserialize(InputBlob& blob, Parent& parent, const Member& member, const Array<T>&)
{
	ASSERT(!member.hasSetter());
	int count = blob.read<int>();
	Array<T>& array = member.getRef(parent);
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


template <typename RootGetter, typename T, typename PP>
struct SetPropertyCommand : IHashedCommand
{
	SetPropertyCommand(const RootGetter& root, PP pp, const T& value)
		: root(root)
		, pp(pp)
		, value(value)
		, old_value(value)
	{
		pp.callWithValue(root(), [&](const auto& v) {
			old_value = v;
		});
	}


	bool execute() override
	{
		pp.setValueFromRoot(root(), value);
		return true;
	}


	void undo() override
	{
		pp.setValueFromRoot(root(), old_value);
	}


	u32 getTypeHash() override
	{
		const auto* root_ptr = &root();
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
		((SetPropertyCommand<RootGetter, T, PP>&)command).value = value;
		return true; 
	}


	T value;
	T old_value;
	PP pp;
	RootGetter root;
};


template <typename RootGetter, typename PP>
struct RemoveArrayItemCommand : IEditorCommand
{
	RemoveArrayItemCommand(const RootGetter& root, PP _pp, int _index, IAllocator& allocator)
		: root(root)
		, pp(_pp)
		, index(_index)
		, blob(allocator)
	{
		auto& array = pp.getRefFromRoot(root());
		::Lumix::serialize(blob, array[index]);
	}


	bool execute() override
	{
		auto& owner = ((typename PP::Base&)pp).getRefFromRoot(root());
		removeFromArray(pp.head, owner, index);
		return true;
	}


	void undo() override
	{
		auto& owner = ((typename PP::Base&)pp).getRefFromRoot(root());
		addToArray(pp.head, owner, index);
		InputBlob input_blob(blob);
		auto& array = pp.getRefFromRoot(root());
		::Lumix::deserialize(input_blob, array[index]);
	}


	void serialize(JsonSerializer& serializer) override { ASSERT(false); }
	void deserialize(JsonSerializer& serializer) override { ASSERT(false); }
	const char* getType() override { return "remove_array_item_anim_editor_property"; }
	bool merge(IEditorCommand& command) override { return false; }

	OutputBlob blob;
	PP pp;
	int index;
	RootGetter root;
};


template <typename RootGetter, typename PropertyPath>
struct AddArrayItemCommand : IEditorCommand
{
	AddArrayItemCommand(const RootGetter& root, PropertyPath _pp)
		: root(root)
		, pp(_pp)
	{}


	bool execute() override
	{
		auto& owner = ((typename PropertyPath::Base&)pp).getRefFromRoot(root());
		addToArray(pp.head, owner, -1);
		return true;
	}


	void undo() override
	{
		auto& owner = ((typename PropertyPath::Base&)pp).getRefFromRoot(root());
		int size = getArraySize(pp.head, owner);
		removeFromArray(pp.head, owner, size - 1);
	}


	void serialize(JsonSerializer& serializer) override { ASSERT(false); }
	void deserialize(JsonSerializer& serializer) override { ASSERT(false); }
	const char* getType() override { return "add_array_item_anim_editor_property"; }
	bool merge(IEditorCommand& command) override { return false; }

	PropertyPath pp;
	RootGetter root;
};


struct PropertyPathBegin
{
	template <typename T>
	T& getRefFromRoot(T& root) const { return root; }
	template <typename T, typename F>
	void callWithValue(const T& root, const F& f) const { f(root); }
};


template <typename Prev, typename Member>
struct PropertyPath : Prev
{
	using Base = Prev;
	using Result = RemoveCVR<typename ResultOf<decltype(&Member::getRef)>::Type>;

	PropertyPath(const Prev& prev, Member _head) : Prev(prev), head(_head), name(_head.name) {}


	template <typename T>
	auto& getRefFromRoot(T& root) const
	{
		return head.getRef(Prev::getRefFromRoot(root));
	}

	template <typename T, typename F>
	void callWithValue(const T& root, const F& f) const
	{
		Prev::callWithValue(root, [&](const auto& parent) {
			head.callWithValue(parent, [&](const auto& value) {
				f(value);
			});
		});
	}


	template <typename T, typename O>
	void setValueFromRoot(T& root, const O& value) const
	{
		decltype(auto) x = Prev::getRefFromRoot(root);
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
	auto& getRefFromRoot(T& root) const
	{
		return Prev::getRefFromRoot(root)[index];
	}

	template <typename T, typename F>
	void callWithValue(T& root, const F& f) const
	{
		Prev::callWithValue(root, [&](const auto& array) {
			f(array[index]);
		});
	}

	template <typename T, typename O>
	void setValueFromRoot(T& root, const O& value)
	{
		Prev::getValueFromRoot(root)[index] = value;
	}

	int index;
};


template <typename F, typename PP, typename Object>
auto generatePP(const F& f, const PP& pp, const Object& object)
{
	f(pp);
}


template <typename F, typename PP, typename Parent, typename... Path>
auto generatePPImpl(int dummy, const F& f, const PP& pp, const Parent& parent, int head, Path... path)
-> decltype(parent[head], void())
{
	auto item_pp = makePP(pp, head);
	decltype(auto) obj = parent[head];
	generatePP(f, item_pp, obj, path...);
}


template <typename F, typename PP, typename Parent, typename... Path>
void generatePPImpl(long dummy, const F& f, const PP& pp, const Parent& parent, int head, Path... path)
{
}


template <typename F, typename PP, typename Parent, typename... Path>
auto generatePP(const F& f, const PP& pp, const Parent& parent, int head, Path... path)
{
	generatePPImpl(0, f, pp, parent, head, path...);
}


template <typename F, typename PP, typename... Path>
void generatePP(const F& f, const PP& pp, const string& parent, const char* head, Path... path)
{
}


template <typename F, typename PP, typename Parent, typename... Path>
void generatePP(const F& f, const PP& pp, const Parent& parent, const char* head, Path... path)
{
	auto decl = getMembers<Parent>();
	apply([&f, &head, &pp, &path..., &parent](const auto& member) {
		if (equalStrings(member.name, head))
		{
			auto child_pp = makePP(pp, member);
			member.callWithValue(parent, [&](const auto& obj) {
				generatePP(f, child_pp, obj, path...);
			});
		}
	}, decl.members);
}


template <typename Root, typename Editor, typename... Path>
void addArrayItem(IAllocator& allocator, Editor& editor, const Root& root, Path... path)
{
	generatePP([&root, &editor, &allocator](const auto& result_pp) {
		using Cmd = AddArrayItemCommand<Root, RemoveCVR<decltype(result_pp)>>;
		auto* cmd = LUMIX_NEW(allocator, Cmd)(root, result_pp);
		editor.executeCommand(*cmd);
	}, PropertyPathBegin(), root(), path...);
}


template <typename RootGetter, typename Editor, typename Value>
struct DoForType
{
	DoForType(const RootGetter& root,
		const Value& value,
		Editor& editor,
		IAllocator& allocator)
		: root(root)
		, value(value)
		, allocator(allocator)
		, editor(editor)
	{}


	template <typename PP>
	typename EnableIf<IsSame<RemoveCVR<typename PP::Result>, RemoveCVR<Value>>::result>::Type
		operator()(const PP& result_pp) const
	{
		using Cmd = SetPropertyCommand<RootGetter, Value, RemoveCVR<decltype(result_pp)>>;
		auto* cmd = LUMIX_NEW(allocator, Cmd)(root, result_pp, value);
		editor.executeCommand(*cmd);
	}
	
	template <typename PP>
	typename EnableIf<!IsSame<RemoveCVR<typename PP::Result>, RemoveCVR<Value>>::result>::Type
		operator()(const PP& result_pp) const
	{
		ASSERT(false);
	}


	IAllocator& allocator;
	RootGetter root;
	const Value& value;
	Editor& editor;
};


template <typename RootGetter, typename Editor, typename Value, typename... Path>
void setPropertyValue(IAllocator& allocator, Editor& editor, const RootGetter& root, const Value& value, Path... path)
{
	DoForType<RootGetter, Editor, Value> do_for_type(root, value, editor, allocator);
	generatePP(do_for_type, PropertyPathBegin(), root(), path...);
}


template <typename RootGetter, typename Editor, typename... Path>
void removeArrayItem(IAllocator& allocator, Editor& editor, const RootGetter& root, int index, Path... path)
{
	generatePP([&root, &editor, &allocator, index](const auto& result_pp) {
		using Cmd = RemoveArrayItemCommand<RootGetter, RemoveCVR<decltype(result_pp)>>;
		auto* cmd = LUMIX_NEW(allocator, Cmd)(root, result_pp, index, allocator);
		editor.executeCommand(*cmd);
	}, PropertyPathBegin(), root(), path...);
}


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


template <typename T> 
int getSubpropertiesCount()
{
	return TupleSize<decltype(getMembers<T>().members)>::result;
}


template <typename Editor, typename RootGetter>
struct UIBuilder
{
	UIBuilder(Editor& editor, const RootGetter& root, IAllocator& allocator)
		: m_editor(editor)
		, m_root(root)
		, m_allocator(allocator)
	{}


	void build()
	{
		auto desc = getMembers<RemoveCVR<decltype(m_root())>>();
		apply([this](const auto& member) {
			auto pp = makePP(PropertyPathBegin{}, member);
			member.callWithValue(m_root(), [this, &pp](const auto& value) {
				this->ui(m_root(), pp, value);
			});
		}, desc.members);
	}


	template <typename O, typename PP, typename T>
	struct CustomUIVisitor
	{
		CustomUIVisitor(O& owner, const PP& pp, const T& obj)
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
		const T& obj;
		bool has_custom_ui = false;
	};


	template <typename T, typename Root>
	class HasUIMethod
	{
		typedef char one;
		typedef long two;

		template <typename C> static one test(decltype(&C::template ui<Root>));
		template <typename C> static two test(...);

	public:
		enum { value = sizeof(test<T>(0)) == sizeof(char) };
	};

	template <typename O, typename PP, typename T>
	auto customUI(O& owner, const PP& pp, const T& obj)
		-> typename EnableIf<HasUIMethod<T, RootGetter>::value, bool>::Type
	{
		CustomUIVisitor<O, PP, T> visitor(owner, pp, obj);
		apply(visitor, pp.head.attributes);
		if (!visitor.has_custom_ui)
		{
			auto& obj_ref = pp.getRefFromRoot(m_root());
			obj_ref.ui(m_editor, m_root);
		}
		return true;
	}

	template <typename O, typename PP, typename T>
	auto customUI(O& owner, const PP& pp, const T& obj) 
		-> typename EnableIf<!HasUIMethod<T, RootGetter>::value, bool>::Type
	{
		CustomUIVisitor<O, PP, T> visitor(owner, pp, obj);
		apply(visitor, pp.head.attributes);
		return visitor.has_custom_ui;
	}


	template <typename O, typename PP, typename T, int capacity>
	void ui(O& owner, const PP& pp, const T (&array)[capacity])
	{
		if (customUI(owner, pp, array)) return;

		bool expanded = ImGui::TreeNodeEx(pp.name, ImGuiTreeNodeFlags_AllowOverlapMode);
		ImGui::SameLine();
		if (ImGui::SmallButton(StaticString<32>("Add")))
		{
			auto* command = LUMIX_NEW(m_allocator, AddArrayItemCommand<RootGetter, PP>)(m_root, pp);
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
					auto* command = LUMIX_NEW(m_allocator, RemoveArrayItemCommand<RootGetter, PP>)(m_root, pp, i);
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
					auto* command = LUMIX_NEW(m_allocator, RemoveArrayItemCommand<RootGetter, PP>)(m_root, pp, i, m_allocator);
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
	void ui(O& owner, const PP& pp, const StaticString<count>& obj)
	{
		if (customUI(owner, pp, obj)) return;

		StaticString<count> tmp = obj;
		if (ImGui::InputText(pp.name, tmp.data, sizeof(tmp.data)))
		{
			auto* command = LUMIX_NEW(m_allocator, SetPropertyCommand<RootGetter, StaticString<count>, PP>)(m_root, pp, tmp);
			m_editor.executeCommand(*command);
		}
	}


	template <typename O, typename PP>
	void ui(O& owner, const PP& pp, int obj)
	{
		if (customUI(owner, pp, obj)) return;

		if (ImGui::InputInt(pp.name, &obj))
		{
			auto* command = LUMIX_NEW(m_allocator, SetPropertyCommand<RootGetter, int, PP>)(m_root, pp, obj);
			m_editor.executeCommand(*command);
		}
	}


	template <typename O, typename PP>
	void ui(O& owner, const PP& pp, bool obj)
	{
		if (customUI(owner, pp, obj)) return;

		if (ImGui::Checkbox(pp.name, &obj))
		{
			auto* command = LUMIX_NEW(m_allocator, SetPropertyCommand<RootGetter, bool, PP>)(m_root, pp, obj);
			m_editor.executeCommand(*command);
		}
	}


	template <typename O, typename PP>
	void ui(O& owner, const PP& pp, float obj)
	{
		if (customUI(owner, pp, obj)) return;

		if (ImGui::DragFloat(pp.name, &obj))
		{
			auto* command = LUMIX_NEW(m_allocator, SetPropertyCommand<RootGetter, float, PP>)(m_root, pp, obj);
			m_editor.executeCommand(*command);
		}
	}


	template <typename O, typename PP, typename T>
	typename EnableIf<TupleSize<decltype(getEnum<T>())>::result == 0>::Type
		ui(O& owner, const PP& pp, const T& obj)
	{
		if (customUI(owner, pp, obj)) return;

		auto desc = getMembers<T>();
		apply([&pp, this](const auto& m) {
			auto child_pp = makePP(pp, m);
			auto& obj = pp.getRefFromRoot(m_root());
			m.callWithValue(obj, [&](const auto& v) {
				this->ui(obj, child_pp, v);
			});
		}, desc.members);
	}


	template <typename O, typename PP, typename T>
	typename EnableIf<TupleSize<decltype(getEnum<T>())>::result != 0>::Type
		ui(O& owner, const PP& pp, const T& obj)
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
			pp.setValueFromRoot(m_root(), val);
		}
	}


	template <typename O, typename PP>
	void ui(O& owner, const PP& pp, const string& value)
	{
		if (customUI(owner, pp, value)) return;

		char tmp[32];
		copyString(tmp, value.c_str());
		if (ImGui::InputText(pp.name, tmp, sizeof(tmp)))
		{
			string tmp_str(tmp, m_allocator);
			auto* command = LUMIX_NEW(m_allocator, SetPropertyCommand<RootGetter, string, PP>)(m_root, pp, tmp_str);
			m_editor.executeCommand(*command);
		}
	}


	template <typename Owner, typename PP, typename T>
	void ui(Owner& owner, const PP& pp, const Array<T>& array)
	{
		if (customUI(owner, pp, array)) return;
		
		bool expanded = ImGui::TreeNodeEx(pp.name, ImGuiTreeNodeFlags_AllowOverlapMode | ImGuiTreeNodeFlags_Framed);
		ImGui::SameLine();
		if (ImGui::SmallButton("Add"))
		{
			auto* command = LUMIX_NEW(m_allocator, AddArrayItemCommand<RootGetter, PP>)(m_root, pp);
			m_editor.executeCommand(*command);
		}
		if (!expanded) return;

		int i = 0;
		int subproperties_count = getSubpropertiesCount<T>();
		if (subproperties_count > 1)
		{
			for (T& item : array)
			{
				StaticString<32> label("", i + 1);
				bool expanded = ImGui::TreeNodeEx(label, ImGuiTreeNodeFlags_AllowOverlapMode);
				ImGui::SameLine();
				if (ImGui::SmallButton("Remove"))
				{
					auto* command = LUMIX_NEW(m_allocator, RemoveArrayItemCommand<RootGetter, PP>)(m_root, pp, i, m_allocator);
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
					auto* command = LUMIX_NEW(m_allocator, RemoveArrayItemCommand<RootGetter, PP>)(m_root, pp, i, m_allocator);
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
	RootGetter m_root;
};


} // namespace Lumix