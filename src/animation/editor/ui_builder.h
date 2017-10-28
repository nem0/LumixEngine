#pragma once



namespace Lumix
{


namespace AnimEditor
{


template <typename T> auto getMembers();


template <typename T, typename... Members>
struct ClassDesc
{
	const char* name;
	Tuple<Members...> members;
};


template <typename Getter, typename Setter, typename... Attrs>
struct RWProperty
{
	using C = typename ClassOf<Getter>::Type;

	auto getValue(C& obj) const { return (obj.*getter)(); }
	template <typename T>
	void setValue(C& obj, const T& value) const { (obj.*setter)(value); }

	const char* name;
	Setter setter;
	Getter getter;
	Tuple<Attrs...> attributes;
};


template <typename Getter, typename... Attrs>
struct ROProperty
{
	using C = typename ClassOf<Getter>::Type;

	decltype(auto) getValue(C& obj) const { return (obj.*getter)(); }
	template <typename T>
	void setValue(C& obj, const T& value) const { ASSERT(false); }

	const char* name;
	Getter getter;
	Tuple<Attrs...> attributes;
};


template <typename Member, typename... Attrs>
struct DataProperty
{
	using C = typename ClassOf<Member>::Type;

	auto& getValue(C& obj) const { return obj.*member; }
	template <typename T>
	void setValue(C& obj, const T& value) const { obj.*member = value; }

	const char* name;
	Member member;
	Tuple<Attrs...> attributes;
};


template <typename T, typename... Members>
auto klass(const char* name, Members... members)
{
	ClassDesc<T, Members...> class_desc;
	class_desc.name = name;
	class_desc.members = makeTuple(members...);
	return class_desc;
}


template <typename R, typename C, typename... Attrs>
auto property(const char* name, R(C::*getter)(), Attrs... attrs)
{
	ROProperty<R(C::*)(), Attrs...> prop;
	prop.name = name;
	prop.getter = getter;
	prop.attributes = makeTuple(attrs...);
	return prop;
}


template <typename T, typename C, typename... Attrs>
auto property(const char* name, T(C::*member), Attrs... attrs)
{
	DataProperty<decltype(member), Attrs...> prop;
	prop.name = name;
	prop.member = member;
	prop.attributes = makeTuple(attrs...);
	return prop;
}


template <typename R, typename C, typename T, typename... Attrs>
auto property(const char* name, R& (C::*getter)(), void (C::*setter)(T), Attrs... attrs)
{
	RWProperty<R&(C::*)(), void (C::*)(T), Attrs...> prop;
	prop.name = name;
	prop.getter = getter;
	prop.setter = setter;
	prop.attributes = makeTuple(attrs...);
	return prop;
}


template <typename Adder, typename Remover>
struct ArrayAttribute
{
	Adder adder;
	Remover remover;
};


template <typename Adder, typename Remover>
auto array_attribute(Adder adder, Remover remover)
{
	ArrayAttribute<Adder, Remover> attr;
	attr.adder = adder;
	attr.remover = remover;
	return attr;
}


struct Serializer
{
	static void serialize(OutputBlob& blob, string& obj)
	{
		blob.write(obj);
	}

	static void serialize(OutputBlob& blob, Array<string>& array)
	{
		blob.write(array.size());
		for (string& item : array)
		{
			blob.write(item);
		}
	}

	template <typename T>
	static void serialize(OutputBlob& blob, Array<T>& obj)
	{
		/*auto l = [&blob, &obj](const auto& member) {
		decltype(auto) x = member.getValue(obj);
		serialize(blob, x);
		};
		apply(l, getMembers<T>().members);*/
		ASSERT(false);
	}

	template <typename T>
	static void serialize(OutputBlob& blob, T& obj)
	{
		auto l = [&blob, &obj](const auto& member) {
			decltype(auto) x = member.getValue(obj);
			serialize(blob, x);
		};
		apply(l, getMembers<T>().members);
	}
};


struct Deserializer
{
	template <typename Root, typename PP, typename T>
	static void deserialize(InputBlob& blob, Root& root, PP& pp, T& obj)
	{
		auto l = [&root, &blob, &obj, &pp](const auto& member) {
			auto child_pp = makePP(pp, member);
			auto& value = child_pp.getValue(obj);
			deserialize(blob, root, child_pp, value);
		};
		apply(l, getMembers<T>().members);
	}


	template <typename Root, typename PP>
	static void deserialize(InputBlob& blob, Root& root, PP& pp, string& obj)
	{
		blob.read(obj);
	}


	template <typename Root, typename PP, typename T>
	static void deserialize(InputBlob& blob, Root& root, PP& pp, Array<T>& array)
	{
		int count = blob.read<int>();
		auto& owner = ((PP::Base&)pp).getValueFromRoot(root);
		AddVisitor<decltype(owner)> visitor(owner, -1);
		for (int i = 0; i < count; ++i)
		{
			apply(visitor, pp.head.attributes);
			deserialize(blob, root, makePP(pp, i), array[i]);
		}
	}

};


template <typename Owner>
struct AddVisitor
{
	AddVisitor(Owner& _owner, int _index) : owner(_owner), index(_index) {}

	template <typename Adder, typename Remover>
	void operator()(const ArrayAttribute<Adder, Remover>& attr) const
	{
		(owner.*attr.adder)(index);
	}

	template <typename T>
	void operator()(const T& x) const {}

	int index;
	Owner& owner;
};


template <typename Owner>
struct RemoveVisitor
{
	RemoveVisitor(Owner& _owner, int _index)
		: owner(_owner)
		, index(_index)
	{}

	template <typename Adder, typename Remover>
	void operator()(const ArrayAttribute<Adder, Remover>& attr) const
	{
		(owner.*attr.remover)(index);
	}

	template <typename T>
	void operator()(const T& x) const {}

	int index;
	Owner& owner;
};


template <typename T, typename PP>
struct SetPropertyCommand : IEditorCommand
{
	SetPropertyCommand(ControllerResource& _controller, PP _pp, T _value)
		: controller(_controller)
		, pp(_pp)
		, value(_value)
		, old_value(_value)
	{}


	bool execute() override
	{
		old_value = pp.getValueFromRoot(controller);
		pp.setValueFromRoot(controller, value);
		return true;
	}


	void undo() override
	{
		pp.setValueFromRoot(controller, old_value);
	}


	void serialize(JsonSerializer& serializer) override { ASSERT(false); }
	void deserialize(JsonSerializer& serializer) override { ASSERT(false); }
	const char* getType() override { return "set_anim_editor_property"; }
	bool merge(IEditorCommand& command) override { return false; }

	T value;
	T old_value;
	PP pp;
	ControllerResource& controller;
};


template <typename PP>
struct RemoveArrayItemCommand : IEditorCommand
{
	RemoveArrayItemCommand(ControllerResource& _controller, PP _pp, int _index)
		: controller(_controller)
		, pp(_pp)
		, index(_index)
		, blob(_controller.getAllocator())
	{}


	bool execute() override
	{
		auto& owner = ((PP::Base&)pp).getValueFromRoot(controller);
		auto& array = pp.getValueFromRoot(controller);
		::Lumix::AnimEditor::Serializer::serialize(blob, array[index]);
		RemoveVisitor<decltype(owner)> visitor(owner, index);
		apply(visitor, pp.head.attributes);
		return true;
	}


	void undo() override
	{
		auto& owner = ((PP::Base&)pp).getValueFromRoot(controller);
		auto& array = pp.getValueFromRoot(controller);
		AddVisitor<decltype(owner)> visitor(owner, index);
		apply(visitor, pp.head.attributes);
		auto item_pp = makePP(pp, index);
		::Lumix::AnimEditor::Deserializer::deserialize(InputBlob(blob), controller, item_pp, item_pp.getValueFromRoot(controller));
	}


	void serialize(JsonSerializer& serializer) override { ASSERT(false); }
	void deserialize(JsonSerializer& serializer) override { ASSERT(false); }
	const char* getType() override { return "remove_array_item_anim_editor_property"; }
	bool merge(IEditorCommand& command) override { return false; }

	OutputBlob blob;
	PP pp;
	int index;
	ControllerResource& controller;
};


template <typename PropertyPath>
struct AddArrayItemCommand : IEditorCommand
{
	AddArrayItemCommand(ControllerResource& _controller, PropertyPath _pp)
		: controller(_controller)
		, pp(_pp)
	{}


	bool execute() override
	{
		auto& owner = ((PropertyPath::Base&)pp).getValueFromRoot(controller);
		auto& array = pp.getValueFromRoot(controller);
		AddVisitor<decltype(owner)> visitor(owner, -1);
		apply(visitor, pp.head.attributes);
		return true;
	}


	void undo() override
	{
		auto& owner = ((PropertyPath::Base&)pp).getValueFromRoot(controller);
		auto& array = pp.getValueFromRoot(controller);
		RemoveVisitor<decltype(owner)> visitor(owner, array.size() - 1);
		apply(visitor, pp.head.attributes);
	}


	void serialize(JsonSerializer& serializer) override { ASSERT(false); }
	void deserialize(JsonSerializer& serializer) override { ASSERT(false); }
	const char* getType() override { return "add_array_item_anim_editor_property"; }
	bool merge(IEditorCommand& command) override { return false; }

	PropertyPath pp;
	ControllerResource& controller;
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

	PropertyPath(Prev& prev, Member _head) : Prev(prev), head(_head), name(_head.name) {}

	template <typename T>
	decltype(auto) getValue(T& obj) const { return head.getValue(obj); }

	template <typename T>
	decltype(auto) getValueFromRoot(T& root) const
	{
		return head.getValue(Prev::getValueFromRoot(root));
	}

	template <typename T, typename O>
	void setValueFromRoot(T& root, O value)
	{
		decltype(auto) x = Prev::getValueFromRoot(root);
		head.setValue(x, value);
	}

	Member head;
	const char* name;
};


template <typename Prev>
struct PropertyPathArray : Prev
{
	using Base = Prev;

	PropertyPathArray(Prev& prev, int _index)
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
auto makePP(Prev& prev, Member head)
{
	return PropertyPath<Prev, RemoveReference<Member>::Type>(prev, head);
}


template <typename Prev>
auto makePP(Prev& prev, int index)
{
	return PropertyPathArray<Prev>(prev, index);
}




} // namespace AnimEditor


} // namespace Lumix