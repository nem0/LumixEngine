#include "core.h"
#include "core/hash_map.h"
#include "core/log.h"
#include "core/stream.h"
#include "engine.h"
#include "engine_hash_funcs.h"
#include "engine/component_types.h"
#include "engine/reflection.h"
#include "world.h"

namespace black {

enum class CoreModuleVersion : i32 {
	SIGNALS,

	LATEST
};

Spline::Spline(IAllocator& allocator)
	: points(allocator)
{}

struct CoreModuleImpl : CoreModule {
	CoreModuleImpl(Engine& engine, ISystem& system, World& world)
		: m_system(system)
		, m_allocator(engine.getAllocator())
		, m_world(world)
		, m_splines(m_allocator)
		, m_signals(m_allocator)
		, m_signal_dispatchers(m_allocator)
	{}

	void serialize(OutputMemoryStream& serializer) override {
		serializer.write((u32)m_signals.size());
		for (UniquePtr<Signal>& signal : m_signals) {
			serializer.write(signal->entity);
			serializer.writeString(signal->event_module ? signal->event_module->name : "");
			serializer.writeString(signal->event ? signal->event->name : "");
			serializer.writeString(signal->function_module ? signal->function_module->name : "");
			serializer.writeString(signal->function ? signal->function->name : "");
		}

		serializer.write((u32)m_splines.size());
		for (auto iter : m_splines.iterated()) {
			const Spline& spline = iter.value();
			serializer.write(iter.key());
			serializer.writeArray(spline.points);
		}
	}
	
	i32 getVersion() const override { return (i32)CoreModuleVersion::LATEST; }

	reflection::Module* findReflectionModule(const char* name) {
		reflection::Module* module = reflection::getFirstModule();
		while (module) {
			if (equalStrings(module->name, name)) return module;
			module = module->next;
		}
		return nullptr;
	}

	reflection::EventBase* findReflectionEvent(reflection::Module* module, const char* name) {
		if (!module) return nullptr;

		for (reflection::EventBase* event : module->events) {
			if (equalStrings(event->name, name)) return event;
		}
		return nullptr;
	}

	reflection::FunctionBase* findReflectionFunction(reflection::Module* module, const char* name) {
		if (!module) return nullptr;

		for (reflection::FunctionBase* fn : module->functions) {
			if (equalStrings(fn->name, name)) return fn;
		}
		return nullptr;
	}

	void deserialize(InputMemoryStream& serializer, const EntityMap& entity_map, i32 version) override {
		u32 count;
		if (version > (i32)CoreModuleVersion::SIGNALS) {
			serializer.read(count);
			m_signals.reserve(m_splines.size() + count);
			for (u32 i = 0; i < count; ++i) {
				UniquePtr<Signal> signal = UniquePtr<Signal>::create(m_allocator);
				EntityRef e;
				serializer.read(e);
				e = entity_map.get(e);
				signal->entity = e;
				const char* event_module_name = serializer.readString();
				const char* event_name = serializer.readString();
				const char* function_module_name = serializer.readString();
				const char* function_name = serializer.readString();
			
				signal->event_module = findReflectionModule(event_module_name);
				signal->event = findReflectionEvent(signal->event_module, event_name);
				signal->function_module = findReflectionModule(function_module_name);
				signal->function = findReflectionFunction(signal->function_module, function_name);
			
				m_signals.insert(e, signal.move());
				m_world.onComponentCreated(e, types::signal, this);
			}
		}

		serializer.read(count);
		m_splines.reserve(m_splines.size() + count);
		for (u32 i = 0; i < count; ++i) {
			Spline spline(m_allocator);
			EntityRef e;
			serializer.read(e);
			e = entity_map.get(e);
			serializer.readArray(&spline.points);
			
			m_splines.insert(e, static_cast<Spline&&>(spline));
			m_world.onComponentCreated(e, types::spline, this);
		}
	}

	struct SignalDispatcher : reflection::EventBase::Callback {
		SignalDispatcher(IAllocator& allocator) : map(allocator) {}

		void invoke(Span<const reflection::Variant> args) override {
			ASSERT(args.length() > 0);
			ASSERT(args[0].type == reflection::Variant::ENTITY);
			EntityPtr e = args[0].e;
			auto iter = map.find(*e);
			if (!iter.isValid()) return;

			Signal* signal = iter.value();
			World& world = core->getWorld();
			IModule* fn_module = world.getModule(signal->function_module->name);
			// TODO ret memory
			signal->function->invoke(fn_module, Span<u8>{}, args);
		}

		HashMap<EntityRef, Signal*> map;
		CoreModuleImpl* core;
	};

	void startGame() override {
		// connect signals
		m_signal_dispatchers.clear();
		for (auto iter : m_signals.iterated()) {
			Signal& signal = *iter.value().get();
			if (!signal.event) continue;
			if (!signal.function) continue;

			IModule* ev_module = m_world.getModule(signal.event_module->name);
			IModule* fn_module = m_world.getModule(signal.function_module->name);
			if (!ev_module) {
				logError("Module ", signal.event_module->name, " not found when trying to connect signal on entity ", iter.key().index);
			}
			if (!fn_module) {
				logError("Module ", signal.function_module->name, " not found when trying to connect signal on entity ", iter.key().index);
			}

			auto dispatcher_iter = m_signal_dispatchers.find(signal.event);
			if (!dispatcher_iter.isValid()) {
				UniquePtr<SignalDispatcher>& ptr = m_signal_dispatchers.insert(signal.event);
				ptr = UniquePtr<SignalDispatcher>::create(m_allocator, m_allocator);
				ptr->core = this;
				dispatcher_iter = m_signal_dispatchers.find(signal.event);
				signal.event->bind(ev_module, dispatcher_iter.value().get());
			}
			dispatcher_iter.value()->map.insert(iter.key(), &signal);
		}
	}

	const char* getName() const override { return "core"; }
	ISystem& getSystem() const override { return m_system; }
	void update(float time_delta) override {}
	World& getWorld() override { return m_world; }

	void createSpline(EntityRef e) override {
		Spline spline(m_allocator);
		m_splines.insert(e, static_cast<Spline&&>(spline));
		m_world.onComponentCreated(e, types::spline, this);
	}
	
	void destroySpline(EntityRef e) override {
		m_splines.erase(e);
		m_world.onComponentDestroyed(e, types::spline, this);
	}
	
	Spline& getSpline(EntityRef e) override {
		return m_splines[e];
	}

	const HashMap<EntityRef, Spline>& getSplines() override { return m_splines; }

	void createSignal(EntityRef e) override {
		UniquePtr<Signal>& s = m_signals.insert(e);
		s = UniquePtr<Signal>::create(m_allocator);
		s->entity = e;
		m_world.onComponentCreated(e, types::signal, this);
	}

	void destroySignal(EntityRef e) override {
		for (UniquePtr<SignalDispatcher>& dispatcher : m_signal_dispatchers) {
			auto iter = dispatcher->map.find(e);
			if (iter.isValid()) {
				dispatcher->map.erase(iter);
			}
		}
		m_signals.erase(e);
		m_world.onComponentDestroyed(e, types::signal, this);
	}

	Signal& getSignal(EntityRef e) override {
		return *m_signals[e].get();
	}

	void connectSignal(EntityRef e, reflection::EventBase* event, reflection::FunctionBase* function) {
		Signal& signal = *m_signals[e].get();
		signal.event = event;
		signal.function = function;
	}

	void getSplineBlob(EntityRef entity, OutputMemoryStream& value) {
		const Spline& spline = m_splines[entity];
		value.writeArray(spline.points);
	}

	void setSplineBlob(EntityRef entity, InputMemoryStream& value) {
		Spline& spline = m_splines[entity];
		value.readArray(&spline.points);
	}

	void getSignalBlob(EntityRef entity, OutputMemoryStream& value) {
		const Signal& signal = *m_signals[entity].get();
		value.write(signal.event_module);
		value.write(signal.event);
		value.write(signal.function_module);
		value.write(signal.function);
	}

	void setSignalBlob(EntityRef entity, InputMemoryStream& value) {
		Signal& signal = *m_signals[entity].get();
		value.read(signal.event_module);
		value.read(signal.event);
		value.read(signal.function_module);
		value.read(signal.function);
	}

	static void reflect() {
		#include "core.gen.h"
	}

	IAllocator& m_allocator;
	HashMap<EntityRef, Spline> m_splines;
	HashMap<EntityRef, UniquePtr<Signal>> m_signals;
	ISystem& m_system;
	World& m_world;
	HashMap<reflection::EventBase*, UniquePtr<SignalDispatcher>> m_signal_dispatchers;
};

struct CorePlugin : ISystem {
	CorePlugin(Engine& engine)
		: m_engine(engine)
	{
		CoreModuleImpl::reflect();
	}

	const char* getName() const override { return "core"; }
	void serialize(OutputMemoryStream& serializer) const override {}
	bool deserialize(i32 version, InputMemoryStream& serializer) override { return version == 0; }

	void createModules(World& world) override {
		IAllocator& allocator = m_engine.getAllocator();
		UniquePtr<CoreModuleImpl> module = UniquePtr<CoreModuleImpl>::create(allocator, m_engine, *this, world);
		world.addModule(module.move());
	}

	Engine& m_engine;
};

ISystem* createCorePlugin(Engine& engine) {
	return BLACK_NEW(engine.getAllocator(), CorePlugin)(engine);
}

} // namespace black