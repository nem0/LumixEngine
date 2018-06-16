#pragma once


#include "editor/ieditor_command.h"
#include "engine/blob.h"
#include "engine/metaprogramming.h"


namespace Lumix
{


struct PathItem
{
	PathItem() {}


	PathItem(const char* name, PathItem* prev)
		: name(name)
		, prev(prev)
	{
		if (prev) prev->next = this;
	}


	PathItem(int index, PathItem* prev)
		: index(index)
		, prev(prev)
	{
		if (prev) prev->next = this;
	}


	const char* name = nullptr;
	int index = -1;
	PathItem* next = nullptr;
	PathItem* prev = nullptr;
};


struct NoUIAttribute {};


template <typename... Attributes>
auto attributes(Attributes... attributes)
{
	return makeTuple(attributes...);
}


template <typename F>
struct Visitor
{
	Visitor(const F& f) : f(f) {}


	template <typename T>
	struct ValueProxy
	{
		ValueProxy(T& value) : value(value) {}
		void addItem(int) const { ASSERT(false); }
		void removeItem(int) const { ASSERT(false); }
		int size() const { ASSERT(false); return -1; }

		template <typename T2> void set(const T2& v) { value = v; }
		template <typename T2> auto setHelper(const T2& v, int) const -> decltype(set(v), void()) { value = v; }
		template <typename T2> void setHelper(const T2& v, char) const { ASSERT(false); }

		template <typename T2> void setValue(const T2& v) const { setHelper(v, 0); }

		T& getValue() const { return value; }
		
		T& value;
	};

	template <typename C, typename T>
	struct PropertyProxy
	{
		PropertyProxy(C* inst, T (C::*getter)() const, void (C::*setter)(T)) 
			: inst(inst)
			, getter(getter)
			, setter(setter)
		{}

		void addItem(int) const { ASSERT(false); }
		void removeItem(int) const { ASSERT(false); }
		int size() const { ASSERT(false); return -1; }
		void setValue(const T& value) const { (inst->*setter)(value); }
		template <typename T2>
		void setValue(const T2& value) const { ASSERT(false); }
		auto getValue() const { return (inst->*getter)(); }

		C* inst;
		T(C::*getter)() const;
		void (C::*setter)(T);
	};

	template <typename T, typename Adder, typename Remover>
	struct ArrayProxy
	{
		ArrayProxy(T& value, const Adder& adder, const Remover& remover) : value(value), adder(adder), remover(remover) {}
		void addItem(int index) const { adder(index); }
		void removeItem(int index) const { remover(index); }
		int size() const { return value.size(); }
		template <typename T2>
		void setValue(const T2& v) const { ASSERT(false); }
		T& getValue() const { return value; }

		T& value;
		Adder adder;
		Remover remover;
	};


	template <typename T>
	void visit(const char* name, T& x)
	{
		if (equalStrings(path->name, name))
		{
			ASSERT(!path->next);
			f(ValueProxy<decltype(x)>(x));
			return;
		}
	}

	template <typename C, typename T>
	void visit(const char* name, C* inst, T (C::*getter)() const, void (C::*setter)(T))
	{
		if (equalStrings(path->name, name))
		{
			ASSERT(!path->next);
			f(PropertyProxy<C, T>(inst, getter, setter));
			return;
		}
	}

	template <typename T, typename Adder, typename Remover, typename Attributes>
	void visit(const char* name, Array<T>& x, Adder adder, Remover remover, const Attributes& attributes)
	{
		if (equalStrings(path->name, name))
		{
			if (!path->next)
			{
				f(ArrayProxy<decltype(x), Adder, Remover>(x, adder, remover));
				return;
			}
			if (!path->next->next)
			{
				int index = path->next->index;
				f(ValueProxy<decltype(x[index])>(x[index]));
				return;
			}
			Visitor<F> v(f);
			v.path = path->next->next;
			x[path->next->index].accept(v);
		}
	}

	template <typename T, typename Adder, typename Remover>
	void visit(const char* name, Array<T>& x, Adder adder, Remover remover)
	{
		visit(name, x, adder, remover, attributes());
	}

	F f;
	PathItem* path;
};


inline bool equalPaths(PathItem* a, PathItem* b)
{
	if (!a && !b) return true;
	if (!a) return false;
	if (!b) return false;
	if (a->index != b->index) return false;
	if (a->name != b->name) return false;

	return equalPaths(a->next, b->next);
}


inline void addToPath(PathItem(&path)[32], int index)
{
	PathItem* p = &path[0];
	while (p->next) p = p->next;
	PathItem* new_tail = p + 1;
	p->next = new_tail;
	new_tail->prev = p;
	new_tail->index = index;
	new_tail->name = nullptr;
}


inline void addToPath(PathItem(&path)[32], const char* name)
{
	PathItem* p = &path[0];
	while (p->next) p = p->next;
	PathItem* new_tail = p + 1;
	p->next = new_tail;
	new_tail->prev = p;
	new_tail->index = -1;
	new_tail->name = name;
}


inline PathItem* getPathRoot(PathItem& path)
{
	PathItem* root = &path;
	if (root->prev) root = root->prev;

	return root;
}

template <int N>
void copyPath(PathItem (&dest)[N], const PathItem* src)
{
	const PathItem* p = src;
	while (p && p->prev) p = p->prev;

	int c = 0;
	while (p)
	{
		ASSERT(c < N);
		dest[c] = *p;
		p = p->next;
		++c;
	}

	for (int i = 0; i < c; ++i)
	{
		dest[i].next = &dest[i + 1];
		dest[i + 1].prev = &dest[i];
	}
	dest[c - 1].next = nullptr;
}


template <typename RootGetter>
struct PathCommand : IEditorCommand
{
	PathCommand(const RootGetter& root_getter) : root_getter(root_getter) {}

	template <typename... Args>
	PathCommand(const RootGetter& root_getter, Args... args)
		: root_getter(root_getter)
	{
		PathItem tmp[] = { PathItem(args, nullptr)... };
		for (int i = 1; i < lengthOf(tmp); ++i)
		{
			tmp[i].prev = &tmp[i - 1];
			tmp[i - 1].next = &tmp[i];
		}
		copyPath(this->items, &tmp[lengthOf(tmp) - 1]);
	}

	PathItem items[32];
	RootGetter root_getter;
};


template <typename RootGetter, typename T>
struct SetCommand : PathCommand<RootGetter>
{
	SetCommand(const RootGetter& root_getter, const PathItem* path, const T& old_value, const T& value)
		: PathCommand<RootGetter>(root_getter)
		, value(value)
		, old_value(old_value)
	{
		copyPath(this->items, path);
	}

	template <typename... Args>
	SetCommand(RootGetter root_getter, const T& old_value, const T& value, Args... args)
		: PathCommand<RootGetter>(root_getter, args...)
		, value(value)
		, old_value(old_value)
	{
	}

	bool execute() override
	{
		auto f = [this] (const auto& x) {
			x.setValue(value);
		};
		Visitor<decltype(f)> v(f);
		v.path = &this->items[0];
		this->root_getter().accept(v);
		return true;
	}


	void undo() override
	{
		auto f = [this](const auto& x) {
			x.setValue(old_value);
		};
		Visitor<decltype(f)> v(f);
		v.path = &this->items[0];
		this->root_getter().accept(v);
	}

	void serialize(JsonSerializer& serializer) override { ASSERT(false); }
	void deserialize(JsonDeserializer& serializer) override { ASSERT(false); }
	const char* getType() override { return "set_property"; }
	

	bool merge(IEditorCommand& command) override 
	{ 
		auto& rhs = static_cast<PathCommand<RootGetter>&>(command);
		if (equalPaths(&this->items[0], &rhs.items[0]))
		{
			static_cast<SetCommand<RootGetter, T>&>(command).value = value;
			return true;
		}
		return false;
	}


	T value;
	T old_value;
};


template <typename RootGetter>
struct AddArrayItemCommand : PathCommand<RootGetter>
{
	AddArrayItemCommand(RootGetter root_getter, const PathItem* path)
		: PathCommand<RootGetter>(root_getter)
	{
		copyPath(this->items, path);
	}

	AddArrayItemCommand(RootGetter root_getter, PathItem* path)
		: PathCommand<RootGetter>(root_getter)
	{
		copyPath(this->items, path);
	}

	template <typename... Args>
	AddArrayItemCommand(RootGetter root_getter, Args... args)
		: PathCommand<RootGetter>(root_getter, args...)
	{
	}

	bool execute() override
	{
		auto f = [this](const auto& x) {
			new_item_index = x.size();
			x.addItem(-1);
		};
		Visitor<decltype(f)> v(f);
		v.path = &this->items[0];
		this->root_getter().accept(v);
		return true;
	}


	void undo() override
	{
		auto f = [this](const auto& x) {
			x.removeItem(new_item_index);
		};
		Visitor<decltype(f)> v(f);
		v.path = &this->items[0];
		this->root_getter().accept(v);
	}

	void serialize(JsonSerializer& serializer) override { ASSERT(false); }
	void deserialize(JsonDeserializer& serializer) override { ASSERT(false); }
	const char* getType() override { return "add_array_item"; }
	bool merge(IEditorCommand& command) override { return false; }


	int new_item_index;
};


struct SerializeVisitor
{
	template <typename T>
	auto visit(const char* name, T& value) -> decltype(value.accept(*this), void()) { value.accept(*this); }

	void visit(const char* name, bool value) { blob->write(value); }
	void visit(const char* name, int value) { blob->write(value); }
	void visit(const char* name, float value) { blob->write(value); }
	void visit(const char* name, u32 value) { blob->write(value); }
	void visit(const char* name, const string& value) { blob->write(value); }
	void visit(const char* name, const Path& value) { blob->writeString(value.c_str()); }
	template <int N>
	void visit(const char* name, const StaticString<N>& value) { blob->write(value); }

	template <typename T>
	void visit(const char* name, Array<T>& value) 
	{ 
		blob->write(value.size());
		for (auto& i : value)
		{
			visit("", i);
		}
	}

	template <typename C, typename T>
	void visit(const char* name, C* inst, T (C::*getter)() const, void (C::*setter)(T)) 
	{
		decltype(auto) x = (inst->*getter)();
		visit(name, x);
	}

	template <typename T, typename Adder, typename Remover, typename Attributes>
	void visit(const char* name, Array<T>& value, const Adder& adder, const Remover& remover, const Attributes&)
	{
		visit(name, value, adder, remover);
	}
		
	template <typename T, typename Adder, typename Remover>
	void visit(const char* name, Array<T>& value, const Adder& adder, const Remover& remover)
	{
		blob->write(value.size());
		for (auto& i : value)
		{
			visit("", i);
		}
	}

	OutputBlob* blob;
};


struct DeserializeVisitor
{
	template <typename T>
	auto visit(const char* name, T& value) -> decltype(value.accept(*this), void()) { value.accept(*this); }
	void visit(const char* name, bool& value) { blob->read(value); }
	void visit(const char* name, int& value) { blob->read(value); }
	void visit(const char* name, float& value) { blob->read(value); }
	void visit(const char* name, u32& value) { blob->read(value); }
	void visit(const char* name, string& value) { blob->read(value); }
	void visit(const char* name, Path& value) 
	{ 
		char tmp[MAX_PATH_LENGTH];
		blob->readString(tmp, lengthOf(tmp)); 
		value = tmp;
	}
	template <int N>
	void visit(const char* name, StaticString<N>& value) { blob->read(value); }

	template <typename T>
	auto visit(const char* name, Array<T>& value, int = 0) -> decltype(T(), void())
	{
		int size;
		blob->read(size);
		for (int i = 0; i < size; ++i)
		{
			value.emplace();
			visit("", value.back());
		}
	}

	template <typename T>
	void visit(const char* name, Array<T>& value, char = 0)
	{
		ASSERT(false);
	}


	template <typename C, typename T>
	void visit(const char* name, C* inst, T(C::*getter)() const, void (C::*setter)(T))
	{
		RemoveCVR<T> val;
		visit(name, val);
		(inst->*setter)(val);

	}

	template <typename T, typename Adder, typename Remover>
	void visit(const char* name, Array<T>& value, const Adder& adder, const Remover& remover)
	{
		int size;
		blob->read(size);
		while (value.size() < size) adder(-1);
		for (int i = 0; i < size; ++i)
		{
			visit("", value[i]);
		}
	}


	template <typename T, typename Adder, typename Remover, typename Attributes>
	void visit(const char* name, Array<T>& value, const Adder& adder, const Remover& remover, const Attributes&)
	{
		visit(name, value, adder, remover);
	}

	InputBlob* blob;
};

template <typename RootGetter>
struct RemoveArrayItemCommand : PathCommand<RootGetter>
{
	RemoveArrayItemCommand(const RootGetter& root_getter, const PathItem* path, int index, IAllocator& allocator)
		: PathCommand<RootGetter>(root_getter)
		, index(index)
		, blob(allocator)
	{
		copyPath(this->items, path);
		serialize();
	}


	template <typename... Args>
	RemoveArrayItemCommand(RootGetter root_getter, int index, IAllocator& allocator, Args... args)
		: PathCommand<RootGetter>(root_getter, args...)
		, index(index)
		, blob(allocator)
	{
		serialize();
	}


	void serialize()
	{
		PathItem tmp[32];
		copyPath(tmp, this->items);
		addToPath(tmp, index);

		auto f = [this](const auto& value) {
			blob.clear();
			SerializeVisitor v;
			v.blob = &blob;
			decltype(auto) x = value.getValue();
			v.visit("", x);
		};

		Visitor<decltype(f)> v(f);
		v.path = &tmp[0];
		this->root_getter().accept(v);
	}


	void deserialize()
	{
		PathItem tmp[32];
		copyPath(tmp, this->items);
		addToPath(tmp, index);

		auto f = [this](const auto& value) {
			DeserializeVisitor v;
			InputBlob input_blob(blob);
			v.blob = &input_blob;
			decltype(auto) x = value.getValue();
			v.visit("", x);
		};

		Visitor<decltype(f)> v(f);
		v.path = &tmp[0];
		this->root_getter().accept(v);
	}


	bool execute() override
	{
		auto f = [this](const auto& x) {
			x.removeItem(index);
		};
		Visitor<decltype(f)> v(f);
		v.path = &this->items[0];
		this->root_getter().accept(v);
		return true;
	}


	void undo() override
	{
		auto f = [this](const auto& x) {
			x.addItem(index);
			this->deserialize();
		};
		Visitor<decltype(f)> v(f);
		v.path = &this->items[0];
		this->root_getter().accept(v);
	}

	void serialize(JsonSerializer& serializer) override { ASSERT(false); }
	void deserialize(JsonDeserializer& serializer) override { ASSERT(false); }
	const char* getType() override { return "remove_array_item"; }
	bool merge(IEditorCommand& command) override { return false; }

	OutputBlob blob;
	int index;
};


template <typename Editor, typename RootGetter>
struct UIVisitor
{
	UIVisitor(Editor& editor, const RootGetter& root_getter, PathItem* parent)
		: root_getter(root_getter)
		, parent(parent)
		, editor(editor)
	{
	}

	void visit(const char* name, string& value)
	{
		char tmp[1024];
		copyString(tmp, value.c_str());
		if (ImGui::InputText(name, tmp, sizeof(tmp)))
		{
			PathItem p(name, parent);
			IAllocator& allocator = editor.getAllocator();
			auto* cmd = LUMIX_NEW(allocator, SetCommand<RootGetter, string>)(root_getter
				, &p
				, string(value.c_str(), allocator)
				, string(tmp, allocator));
			editor.executeCommand(*cmd);
		}
	}
		
	template <int N>
	void visit(const char* name, StaticString<N>& value)
	{
		char tmp[N];
		copyString(tmp, value);
		if (ImGui::InputText(name, tmp, sizeof(tmp)))
		{
			PathItem p(name, parent);
			auto* cmd = LUMIX_NEW(editor.getAllocator(), SetCommand<RootGetter, StaticString<N>>)(root_getter
				, &p
				, value
				, StaticString<N>(tmp));
			editor.executeCommand(*cmd);
		}
	}

	template <typename C, int N>
	void visit(const char* name, C* inst, const StaticString<N>& (C::*getter)() const, void (C::*setter)(const StaticString<N>&))
	{
		char tmp[N];
		const StaticString<N>& value = (inst->*getter)();
		copyString(tmp, value);
		if (ImGui::InputText(name, tmp, sizeof(tmp)))
		{
			PathItem p(name, parent);
			auto* cmd = LUMIX_NEW(editor.getAllocator(), SetCommand<RootGetter, StaticString<N>>)(root_getter
				, &p
				, value
				, StaticString<N>(tmp));
			editor.executeCommand(*cmd);
		}
	}

	template <typename C>
	void visit(const char* name, C* inst, const Path& (C::*getter)() const, void (C::*setter)(const Path&))
	{
		char tmp[MAX_PATH_LENGTH];
		const Path& value = (inst->*getter)();
		copyString(tmp, value.c_str());
		if (ImGui::InputText(name, tmp, sizeof(tmp)))
		{
			PathItem p(name, parent);
			auto* cmd = LUMIX_NEW(editor.getAllocator(), SetCommand<RootGetter, Path>)(root_getter
				, &p
				, value
				, Path(tmp));
			editor.executeCommand(*cmd);
		}
	}


	template <typename C>
	void visit(const char* name, C* inst, bool (C::*getter)() const, void (C::*setter)(bool))
	{
		bool value = (inst->*getter)();
		if (ImGui::Checkbox(name, &value)) 
		{
			PathItem p(name, parent);
			auto* cmd = LUMIX_NEW(editor.getAllocator(), SetCommand<RootGetter, bool>)(root_getter
				, &p
				, !value
				, value);
			editor.executeCommand(*cmd);
		}
	}

	template <typename C>
	void visit(const char* name, C* inst, u32 (C::*getter)() const, void (C::*setter)(u32))
	{
		ASSERT(false);
	}

	template <typename C>
	void visit(const char* name, C* inst, float (C::*getter)() const, void (C::*setter)(float))
	{
		float value = (inst->*getter)();
		float old_value = value;
		if (ImGui::InputFloat(name, &value))
		{
			PathItem p(name, parent);
			auto* cmd = LUMIX_NEW(editor.getAllocator(), SetCommand<RootGetter, float>)(root_getter
				, &p
				, old_value
				, value);
			editor.executeCommand(*cmd);
		}
	}

	template <typename C, typename T>
	void visit(const char* name, C* inst, T (C::*getter)() const, void (C::*setter)(T))
	{
		ASSERT(false);
	}

	template <typename T>
	auto customUI(const char* name, T& value, int) -> decltype(value.ui(name), bool())
	{
		value.ui(name);
		return true;
	}

	template <typename T>
	auto customUI(const char* name, T& value, int) -> decltype(value.edit(*this, name), bool())
	{
		value.edit(*this, name);
		return true;
	}

	template <typename T>
	bool customUI(const char*, T& value, char)
	{
		return false;
	}

	template <typename T>
	void visit(const char* name, T& value)
	{
		if (!customUI(name, value, 0))
		{
			value.accept(*this);
		}
	}

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

	template <typename T>
	auto customArrayItemUI(PathItem& path, T& item, int) -> decltype(item.ui(), bool())
	{
		return item.ui();
	}

	template <typename T>
	bool customArrayItemUI(PathItem& path, T& item, char)
	{
		auto f = [](const auto& v) {};

		Visitor<decltype(f)> v(f);
		v.path = getPathRoot(path);
		v.visit("", item);
		return false;
	}

	template <typename T, typename Adder, typename Remover, typename Attributes>
	void visit(const char* name, Array<T>& value, const Adder& adder, const Remover& remover, const Attributes& attrs)
	{
		if (TupleContains<NoUIAttribute, Attributes>::value) return;

		bool expanded = ImGui::TreeNodeEx(name, ImGuiTreeNodeFlags_AllowItemOverlap | ImGuiTreeNodeFlags_Framed);
		ImGui::SameLine();
		if (ImGui::SmallButton("Add"))
		{
			PathItem p(name, parent);
			auto* command = LUMIX_NEW(editor.getAllocator(), AddArrayItemCommand<RootGetter>)(root_getter, &p);
			editor.executeCommand(*command);
		}
		if (!expanded) return;
		for (int i = 0, c = value.size(); i < c; ++i)
		{
			T& item = value[i];
			StaticString<32> label("", i + 1);
			bool expanded = ImGui::TreeNodeEx(label, ImGuiTreeNodeFlags_AllowItemOverlap);
			ImGui::SameLine();
			if (ImGui::SmallButton("Remove"))
			{
				PathItem p(name, parent);
				auto* command = LUMIX_NEW(editor.getAllocator(), RemoveArrayItemCommand<RootGetter>)(root_getter, &p, i, editor.getAllocator());
				editor.executeCommand(*command);
				if (expanded) ImGui::TreePop();
				break;
			}

			if (expanded)
			{
				PathItem p0(name, parent);
				PathItem p1(i, &p0);

				if (!customArrayItemUI(p1, item, 0)) {
					UIVisitor<Editor, RootGetter> item_visitor(editor, root_getter, &p1);
					item_visitor.visit("", item);
				}
				ImGui::TreePop();
			}
		}

		ImGui::TreePop();
	}

	template <typename T, typename Adder, typename Remover>
	void visit(const char* name, Array<T>& value, const Adder& adder, const Remover& remover)
	{
		visit(name, value, adder, remover, attributes());
	}

	PathItem* parent;
	RootGetter root_getter;
	Editor& editor;
};


template <typename C, typename T>
struct NoUIProxy
{
	C* inst;
	T(C::*getter)() const;
	void (C::*setter)(T);

	void ui(const char* name) {}

	void operator=(T value) { setValue(value); }

	T getValue() const { return (inst->*getter)(); }
	void setValue(T value) { (inst->*setter)(value); }

	template <typename V>
	void accept(V& visitor) { visitor.visit("", this, &NoUIProxy::getValue, &NoUIProxy::setValue); }
};


template <typename C, typename T>
struct EnumProxy
{
	C* inst;
	T(C::*getter)() const;
	void (C::*setter)(T);
	const char* (*value_to_string)(int);

	template <typename V>
	void edit(V& visitor, const char* name)
	{
		int value = getValue();
		const int old_value = value;

		if (ImGui::BeginCombo(name, value_to_string(old_value))) {
			int i = 0;
			const char* combo_value = value_to_string(i);
			while (combo_value) 
			{
				if (ImGui::Selectable(combo_value))
				{
					IAllocator& allocator = visitor.editor.getAllocator();
					PathItem items[32];
					copyPath(items, visitor.parent);
					addToPath(items, name);
					auto* cmd = LUMIX_NEW(allocator, SetCommand<decltype(visitor.root_getter), int>)(visitor.root_getter, items, old_value, i);
					visitor.editor.executeCommand(*cmd);
				}
				++i;
				combo_value = value_to_string(i);
			}
			ImGui::EndCombo();
		}
	}

	void operator=(int value) { setValue(value); }

	int getValue() const { return (inst->*getter)(); }
	void setValue(int value) { (inst->*setter)((T)value); }

	template <typename V>
	void accept(V& visitor) { visitor.visit("", this, &EnumProxy::getValue, &EnumProxy::setValue); }
};


template <typename C, typename T>
auto enum_proxy(const char* (*value_to_string)(int), C* inst, T(C::*getter)() const, void (C::*setter)(T))
{
	EnumProxy<C, T> e;
	e.inst = inst;
	e.getter = getter;
	e.setter = setter;
	e.value_to_string = value_to_string;
	return e;
}


template <typename C, typename T>
auto no_ui_proxy(C* inst, T(C::*getter)() const, void (C::*setter)(T))
{
	NoUIProxy<C, T> e;
	e.inst = inst;
	e.getter = getter;
	e.setter = setter;
	return e;
}


template <typename Editor, typename RootGetter>
void buildUI(Editor& editor, RootGetter& root_getter)
{
	UIVisitor<Editor, RootGetter> foo(editor, root_getter, nullptr);
	root_getter().accept(foo);
};


} // namespace Lumix