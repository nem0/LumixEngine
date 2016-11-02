#define _CRT_SECURE_NO_WARNINGS
#include "steam_api.h"
#include "engine/engine.h"
#include "engine/hash_map.h"
#include "engine/iallocator.h"
#include "engine/iplugin.h"
#include "engine/log.h"
#include "engine/lua_wrapper.h"
#include "engine/resource_manager.h"
#include "renderer/texture.h"
#include "renderer/texture_manager.h"


using namespace Lumix;


static const ResourceType TEXTURE_TYPE("texture");


struct SteamPlugin : public IPlugin
{
	SteamPlugin(Engine& engine) 
		: m_engine(engine)
		, m_avatars(engine.getAllocator())
	{
		if (!SteamAPI_Init() || !SteamHTMLSurface()->Init())
		{
			g_log_error.log("Failed to init steam");
		}
		registerLuaAPI();
	}

	~SteamPlugin()
	{
		SteamAPI_Shutdown();
	}

	const char* getName() const override { return "steam"; }
	virtual void serialize(OutputBlob&)  {}
	virtual void deserialize(InputBlob&)  {}
	virtual void update(float) { SteamAPI_RunCallbacks(); }

	virtual IScene* createScene(Universe&) { return nullptr; }
	virtual void destroyScene(IScene*)  {}
	virtual void startGame() {}
	virtual void stopGame()
	{
		for (Texture* tex : m_avatars)
		{
			tex->destroy();
			LUMIX_DELETE(m_engine.getAllocator(), tex);
		}
		m_avatars.clear();
	}


	static const char* LUA_GetPersonaName() { return SteamFriends() ? SteamFriends()->GetPersonaName() : ""; }
	
	
	static int LUA_GetFriendCount() { return SteamFriends() ? SteamFriends()->GetFriendCount(k_EFriendFlagAll) : 0; }
	
	
	static const char* LUA_GetFriendPersonaName(int friend_idx)
	{
		ISteamFriends* friends = SteamFriends();
		if (!friends) return "";
		CSteamID id = friends->GetFriendByIndex(friend_idx, k_EFriendFlagAll);
		return friends->GetFriendPersonaName(id);
	}


	static int LUA_GetFriendPersonaState(int friend_idx)
	{
		ISteamFriends* friends = SteamFriends();
		if (!friends) return 0;
		CSteamID id = friends->GetFriendByIndex(friend_idx, k_EFriendFlagAll);
		return (int)friends->GetFriendPersonaState(id);
	}


	static bgfx::TextureHandle* LUA_GetAvatar(lua_State* L, int friend_idx)
	{
		if (lua_getglobal(L, "__SteamPlugin") != LUA_TLIGHTUSERDATA)
		{
			ASSERT(false);
			lua_pop(L, 1);
			return nullptr;
		}
		
		auto* that = LuaWrapper::toType<SteamPlugin*>(L, -1);
		lua_pop(L, 1);

		IAllocator& allocator = that->m_engine.getAllocator();

		ISteamFriends* friends = SteamFriends();
		if (!friends) return nullptr;

		CSteamID id = friends->GetFriendByIndex(friend_idx, k_EFriendFlagAll);
		auto iter = that->m_avatars.find(id.ConvertToUint64());
		if (iter.isValid()) return &iter.value()->handle;

		int avatar = friends->GetMediumFriendAvatar(id);
		Lumix::uint32 w, h;
		SteamUtils()->GetImageSize(avatar, &w, &h);
		if (w == 0 || h == 0) return nullptr;
		
		Array<Lumix::uint8> data(allocator);
		data.resize(w * h * 4);
		SteamUtils()->GetImageRGBA(avatar, &data[0], w * h * 4);

		TextureManager* mng = static_cast<TextureManager*>(that->m_engine.getResourceManager().get(TEXTURE_TYPE));
		Texture* tex = LUMIX_NEW(allocator, Texture)(Path("avatar"), *mng, allocator);
		tex->create(w, h, &data[0]);
		that->m_avatars.insert(id.ConvertToUint64(), tex);
		return &tex->handle;
	}


	static void LUA_TriggerScreenshot()
	{
		SteamScreenshots()->TriggerScreenshot();
	}


	void registerLuaAPI()
	{
		lua_State* L = m_engine.getState();

		#define REGISTER_FUNCTION(group, func) \
			LuaWrapper::createSystemFunction(L, #group, #func, \
				&LuaWrapper::wrap<decltype(&LUA_##func), LUA_##func>); \
		\

		REGISTER_FUNCTION(SteamFriends, GetPersonaName);
		REGISTER_FUNCTION(SteamFriends, GetFriendCount);
		REGISTER_FUNCTION(SteamFriends, GetFriendPersonaName);
		REGISTER_FUNCTION(SteamFriends, GetFriendPersonaState);
		REGISTER_FUNCTION(SteamFriends, GetAvatar);
		REGISTER_FUNCTION(SteamScreenshots, TriggerScreenshot);

		lua_pushlightuserdata(L, this);
		lua_setglobal(L, "__SteamPlugin");

		#undef REGISTER_FUNCTION
	}

	Engine& m_engine;
	HashMap<::uint64, Texture*> m_avatars;
};


LUMIX_PLUGIN_ENTRY(steam)
{
	return LUMIX_NEW(engine.getAllocator(), SteamPlugin)(engine);
}
