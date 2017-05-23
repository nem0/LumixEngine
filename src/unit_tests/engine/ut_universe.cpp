#include "engine/path.h"
#include "engine/universe/universe.h"
#include "unit_tests/suite/lumix_unit_tests.h"


using namespace Lumix;


namespace
{
	void UT_universe_hierarchy(const char* params)
	{
		DefaultAllocator allocator;
		PathManager path_manager(allocator);
		Universe universe(allocator);

		Entity e0 = universe.createEntity({0, 0, 0}, {0, 0, 0, 1});
		Entity e1 = universe.createEntity({0, 0, 0}, {0, 0, 0, 1});
		Entity e2 = universe.createEntity({0, 0, 0}, {0, 0, 0, 1});
		Entity e3 = universe.createEntity({0, 0, 0}, {0, 0, 0, 1});

		LUMIX_EXPECT(!universe.getParent(e0).isValid());
		LUMIX_EXPECT(!universe.getParent(e1).isValid());
		LUMIX_EXPECT(!universe.getParent(e2).isValid());
		LUMIX_EXPECT(!universe.getParent(e3).isValid());

		LUMIX_EXPECT(!universe.getFirstChild(e0).isValid());
		LUMIX_EXPECT(!universe.getFirstChild(e1).isValid());
		LUMIX_EXPECT(!universe.getFirstChild(e2).isValid());
		LUMIX_EXPECT(!universe.getFirstChild(e3).isValid());

		universe.setParent(e0, e1);

		LUMIX_EXPECT(!universe.getNextSibling(e0).isValid());
		LUMIX_EXPECT(!universe.getNextSibling(e1).isValid());

		LUMIX_EXPECT(!universe.getFirstChild(e1).isValid());
		LUMIX_EXPECT(!universe.getFirstChild(e2).isValid());
		LUMIX_EXPECT(!universe.getFirstChild(e3).isValid());
		LUMIX_EXPECT(!universe.getParent(e0).isValid());
		LUMIX_EXPECT(!universe.getParent(e2).isValid());
		LUMIX_EXPECT(!universe.getParent(e3).isValid());

		LUMIX_EXPECT(universe.getFirstChild(e0).isValid());
		LUMIX_EXPECT(universe.getParent(e1) == e0);

		universe.setParent(e0, e2);

		LUMIX_EXPECT(!universe.getFirstChild(e1).isValid());
		LUMIX_EXPECT(!universe.getFirstChild(e2).isValid());
		LUMIX_EXPECT(!universe.getFirstChild(e3).isValid());
		LUMIX_EXPECT(!universe.getParent(e0).isValid());
		LUMIX_EXPECT(!universe.getParent(e3).isValid());

		LUMIX_EXPECT(universe.getFirstChild(e0).isValid());
		LUMIX_EXPECT(universe.getParent(e1) == e0);
		LUMIX_EXPECT(universe.getParent(e2) == e0);

		LUMIX_EXPECT(!universe.getNextSibling(e0).isValid());
		LUMIX_EXPECT(universe.getNextSibling(e1).isValid() != universe.getNextSibling(e2).isValid());

		universe.setParent(e2, e3);

		LUMIX_EXPECT(!universe.getFirstChild(e1).isValid());
		LUMIX_EXPECT(universe.getFirstChild(e2) == e3);
		LUMIX_EXPECT(!universe.getFirstChild(e3).isValid());
		LUMIX_EXPECT(!universe.getParent(e0).isValid());
		LUMIX_EXPECT(universe.getParent(e3) == e2);

		LUMIX_EXPECT(universe.getFirstChild(e0).isValid());
		LUMIX_EXPECT(universe.getParent(e1) == e0);
		LUMIX_EXPECT(universe.getParent(e2) == e0);

		LUMIX_EXPECT(!universe.getNextSibling(e0).isValid());
		LUMIX_EXPECT(!universe.getNextSibling(e3).isValid());
		LUMIX_EXPECT(universe.getNextSibling(e1).isValid() != universe.getNextSibling(e2).isValid());

		universe.setParent(INVALID_ENTITY, e2);

		LUMIX_EXPECT(!universe.getFirstChild(e1).isValid());
		LUMIX_EXPECT(universe.getFirstChild(e2) == e3);
		LUMIX_EXPECT(!universe.getFirstChild(e3).isValid());
		LUMIX_EXPECT(!universe.getParent(e0).isValid());
		LUMIX_EXPECT(universe.getParent(e3) == e2);

		LUMIX_EXPECT(universe.getFirstChild(e0) == e1);
		LUMIX_EXPECT(universe.getParent(e1) == e0);
		LUMIX_EXPECT(!universe.getParent(e2).isValid());

		LUMIX_EXPECT(!universe.getNextSibling(e0).isValid());
		LUMIX_EXPECT(!universe.getNextSibling(e1).isValid());
		LUMIX_EXPECT(!universe.getNextSibling(e2).isValid());
		LUMIX_EXPECT(!universe.getNextSibling(e3).isValid());

		universe.setParent(INVALID_ENTITY, e1);

		LUMIX_EXPECT(!universe.getFirstChild(e1).isValid());
		LUMIX_EXPECT(universe.getFirstChild(e2) == e3);
		LUMIX_EXPECT(!universe.getFirstChild(e3).isValid());
		LUMIX_EXPECT(!universe.getParent(e0).isValid());
		LUMIX_EXPECT(universe.getParent(e3) == e2);

		LUMIX_EXPECT(!universe.getFirstChild(e0).isValid());
		LUMIX_EXPECT(!universe.getParent(e1).isValid());
		LUMIX_EXPECT(!universe.getParent(e2).isValid());

		LUMIX_EXPECT(!universe.getNextSibling(e2).isValid());
		LUMIX_EXPECT(!universe.getNextSibling(e3).isValid());

		universe.setParent(e1, e2);

		LUMIX_EXPECT(!universe.getFirstChild(e0).isValid());
		LUMIX_EXPECT(universe.getFirstChild(e1) == e2);
		LUMIX_EXPECT(universe.getFirstChild(e2) == e3);
		LUMIX_EXPECT(!universe.getFirstChild(e3).isValid());

		LUMIX_EXPECT(!universe.getParent(e0).isValid());
		LUMIX_EXPECT(!universe.getParent(e1).isValid());
		LUMIX_EXPECT(universe.getParent(e2) == e1);
		LUMIX_EXPECT(universe.getParent(e3) == e2);

		LUMIX_EXPECT(!universe.getNextSibling(e1).isValid());
		LUMIX_EXPECT(!universe.getNextSibling(e2).isValid());
		LUMIX_EXPECT(!universe.getNextSibling(e3).isValid());
	}


	void UT_universe_hierarchy2(const char* params)
	{
		DefaultAllocator allocator;
		PathManager path_manager(allocator);
		Universe universe(allocator);

		Entity e0 = universe.createEntity({ 0, 0, 0 }, { 0, 0, 0, 1 });
		Entity e1 = universe.createEntity({ 0, 0, 0 }, { 0, 0, 0, 1 });
		Entity e2 = universe.createEntity({ 0, 0, 0 }, { 0, 0, 0, 1 });
		Entity e3 = universe.createEntity({ 0, 0, 0 }, { 0, 0, 0, 1 });

		universe.setParent(e0, e1);
		universe.setParent(e0, e2);
		universe.setParent(e2, e3);

		universe.destroyEntity(e2);

		LUMIX_EXPECT(!universe.getNextSibling(e1).isValid());
		LUMIX_EXPECT(!universe.getParent(e3).isValid());
	}


	void UT_universe_hierarchy3(const char* params)
	{
		DefaultAllocator allocator;
		PathManager path_manager(allocator);
		Universe universe(allocator);

		Entity e0 = universe.createEntity({ 0, 0, 0 }, { 0, 0, 0, 1 });
		Entity e1 = universe.createEntity({ 0, 0, 0 }, { 0, 0, 0, 1 });
		Entity e2 = universe.createEntity({ 0, 0, 0 }, { 0, 0, 0, 1 });
		Entity e3 = universe.createEntity({ 0, 0, 0 }, { 0, 0, 0, 1 });

		universe.setParent(e0, e1);
		universe.setParent(e0, e2);
		universe.setParent(e0, e3);

		universe.destroyEntity(e1);
		universe.destroyEntity(e2);
		universe.destroyEntity(e3);

		LUMIX_EXPECT(!universe.getParent(e1).isValid());
		LUMIX_EXPECT(!universe.getParent(e2).isValid());
		LUMIX_EXPECT(!universe.getParent(e3).isValid());
	}


	void UT_universe(const char* params)
	{
		DefaultAllocator allocator;
		PathManager path_manager(allocator);
		Universe universe(allocator);
		LUMIX_EXPECT(!universe.hasEntity(INVALID_ENTITY));
		LUMIX_EXPECT(!universe.hasEntity({0}));
		LUMIX_EXPECT(!universe.hasEntity({1}));
		LUMIX_EXPECT(!universe.hasEntity({100}));

		static const int ENTITY_COUNT = 5;

		Vec3 p(0, 0, 0);
		Quat r(0, 0, 0, 1);
		Entity entities[ENTITY_COUNT];
		for (int i = 0; i < ENTITY_COUNT; ++i)
		{
			entities[i] = universe.createEntity(p, r);
		}
		universe.destroyEntity(entities[4]);
		entities[4] = universe.createEntity(p, r);

		universe.destroyEntity(entities[3]);
		entities[3] = universe.createEntity(p, r);

		universe.destroyEntity(entities[3]);
		universe.destroyEntity(entities[4]);
		entities[3] = universe.createEntity(p, r);
		entities[4] = universe.createEntity(p, r);

		universe.destroyEntity(entities[4]);
		universe.destroyEntity(entities[3]);
		entities[3] = universe.createEntity(p, r);
		entities[4] = universe.createEntity(p, r);

		universe.destroyEntity(entities[0]);
		universe.destroyEntity(entities[0]);
		entities[0] = universe.createEntity(p, r);

		for (int i = 0; i < ENTITY_COUNT; ++i)
		{
			universe.setPosition(entities[i], float(i), float(i), float(i));
			Vec3 pos = universe.getPosition(entities[i]);
			LUMIX_EXPECT_CLOSE_EQ(pos.x, float(i), 0.00001f);
			LUMIX_EXPECT_CLOSE_EQ(pos.y, float(i), 0.00001f);
			LUMIX_EXPECT_CLOSE_EQ(pos.z, float(i), 0.00001f);
		}
		universe.destroyEntity(entities[1]);
		for (int i = 0; i < ENTITY_COUNT; ++i)
		{
			if (i == 1) continue;
			universe.setPosition(entities[i], float(i), float(i), float(i));
			Vec3 pos = universe.getPosition(entities[i]);
			LUMIX_EXPECT_CLOSE_EQ(pos.x, float(i), 0.00001f);
			LUMIX_EXPECT_CLOSE_EQ(pos.y, float(i), 0.00001f);
			LUMIX_EXPECT_CLOSE_EQ(pos.z, float(i), 0.00001f);
		}
		entities[1] = universe.createEntity(p, r);
		for (int i = 0; i < ENTITY_COUNT; ++i)
		{
			universe.setPosition(entities[i], float(i), float(i), float(i));
			Vec3 pos = universe.getPosition(entities[i]);
			LUMIX_EXPECT_CLOSE_EQ(pos.x, float(i), 0.00001f);
			LUMIX_EXPECT_CLOSE_EQ(pos.y, float(i), 0.00001f);
			LUMIX_EXPECT_CLOSE_EQ(pos.z, float(i), 0.00001f);
		}
	}
} // anonymous namespace

REGISTER_TEST("unit_tests/engine/universe", UT_universe, "");
REGISTER_TEST("unit_tests/engine/universe/hierarchy", UT_universe_hierarchy, "");
REGISTER_TEST("unit_tests/engine/universe/hierarchy2", UT_universe_hierarchy2, "");
REGISTER_TEST("unit_tests/engine/universe/hierarchy3", UT_universe_hierarchy3, "");
