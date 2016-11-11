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
			g_log_error.log("Steam") << "Failed to init steam";
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

	virtual void createScenes(Universe&) {}
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
	

	static ::uint64 LUA_GetFriendByIndex(int friend_idx)
	{
		return SteamFriends()->GetFriendByIndex(friend_idx, k_EFriendFlagAll).ConvertToUint64();
	}

	
	static const char* LUA_GetFriendPersonaName(::uint64 steam_id)
	{
		return SteamFriends()->GetFriendPersonaName(steam_id);
	}


	static int LUA_GetFriendPersonaState(::uint64 steam_id)
	{
		return (int)SteamFriends()->GetFriendPersonaState(steam_id);
	}


	static SteamPlugin* getLuaSteamPlugin(lua_State* L)
	{
		if (lua_getglobal(L, "__SteamPlugin") != LUA_TLIGHTUSERDATA)
		{
			ASSERT(false);
			lua_pop(L, 1);
			return nullptr;
		}

		auto* plugin = LuaWrapper::toType<SteamPlugin*>(L, -1);
		lua_pop(L, 1);
		return plugin;
	}


	static bgfx::TextureHandle* LUA_GetAvatar(lua_State* L, int friend_idx)
	{
		auto* that = getLuaSteamPlugin(L);
		if (!that) return nullptr;

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

	
	static SteamAPICall_t LUA_FindLeaderboard(lua_State* L, const char* leaderboard_name)
	{
		SteamAPICall_t apicall = SteamUserStats()->FindOrCreateLeaderboard(leaderboard_name, k_ELeaderboardSortMethodDescending, k_ELeaderboardDisplayTypeNumeric);
		return apicall;
	}


	static const char* LUA_GetLeaderboardName(lua_State* L, SteamLeaderboard_t leaderboard)
	{
		return SteamUserStats()->GetLeaderboardName(leaderboard);
	}


	static int LUA_GetLeaderboardEntryCount(lua_State* L, SteamLeaderboard_t leaderboard)
	{
		return SteamUserStats()->GetLeaderboardEntryCount(leaderboard);
	}


	static int LUA_GetDownloadedLeaderboardEntry(lua_State* L)
	{
		SteamLeaderboardEntries_t entries = LuaWrapper::checkArg<SteamLeaderboardEntries_t>(L, 1);
		int index = LuaWrapper::checkArg<int>(L, 2);
		LeaderboardEntry_t entry;
		bool b = SteamUserStats()->GetDownloadedLeaderboardEntry(entries, index, &entry, nullptr, 0);
		if (!b) return 0;
		
		lua_newtable(L);
		lua_pushinteger(L, entry.m_steamIDUser.ConvertToUint64());
		lua_setfield(L, -2, "m_steamIDUser");
		lua_pushinteger(L, entry.m_nScore);
		lua_setfield(L, -2, "m_nScore");
		return 1;
	}


	static SteamAPICall_t LUA_DownloadLeaderboardEntries(lua_State* L, SteamLeaderboard_t leaderboard, int range_start, int range_end)
	{
		auto apicall =  SteamUserStats()->DownloadLeaderboardEntries(leaderboard, k_ELeaderboardDataRequestGlobal, range_start, range_end);
		return apicall;
	}


	static bool LUA_IsAPICallCompleted(SteamAPICall_t apicall)
	{
		bool failed;
		bool b = SteamUtils()->IsAPICallCompleted(apicall, &failed);
		return b && !failed;
	}


	static int LUA_GetLeaderboardScoresDownloaded(lua_State* L)
	{
		SteamAPICall_t apicall = LuaWrapper::checkArg<SteamAPICall_t>(L, 1);
		bool failed;
		LeaderboardScoresDownloaded_t res;
		bool b = SteamUtils()->GetAPICallResult(apicall, &res, sizeof(res), LeaderboardScoresDownloaded_t::k_iCallback, &failed);
		if (failed)	return 0;
		if (!b) return 0;

		lua_newtable(L);
		lua_pushinteger(L, res.m_hSteamLeaderboardEntries);
		lua_setfield(L, -2, "m_hSteamLeaderboardEntries");
		lua_pushinteger(L, res.m_cEntryCount);
		lua_setfield(L, -2, "m_cEntryCount");
		return 1;

	}


	static int LUA_GetLeaderboardFindResult(lua_State* L)
	{
		SteamAPICall_t apicall = LuaWrapper::checkArg<SteamAPICall_t>(L, 1);
		bool failed;
		LeaderboardFindResult_t res;
		bool b = SteamUtils()->GetAPICallResult(apicall, &res, sizeof(res), LeaderboardFindResult_t::k_iCallback, &failed);
		if (failed)	return 0;
		if (!b) return 0;

		lua_newtable(L);
		lua_pushinteger(L, res.m_hSteamLeaderboard);
		lua_setfield(L, -2, "m_hSteamLeaderboard");
		lua_pushinteger(L, res.m_bLeaderboardFound);
		lua_setfield(L, -2, "m_bLeaderboardFound");
		return 1;
	}


	void registerLuaAPI()
	{
		lua_State* L = m_engine.getState();

		#define REGISTER_FUNCTION(group, func) \
			LuaWrapper::createSystemFunction(L, #group, #func, \
				&LuaWrapper::wrap<decltype(&LUA_##func), LUA_##func>) \

		REGISTER_FUNCTION(SteamFriends, GetPersonaName);
		REGISTER_FUNCTION(SteamFriends, GetFriendCount);
		REGISTER_FUNCTION(SteamFriends, GetFriendPersonaName);
		REGISTER_FUNCTION(SteamFriends, GetFriendPersonaState);
		REGISTER_FUNCTION(SteamFriends, GetAvatar);
		REGISTER_FUNCTION(SteamScreenshots, TriggerScreenshot);
		REGISTER_FUNCTION(SteamUserStats, FindLeaderboard);
		REGISTER_FUNCTION(SteamUserStats, DownloadLeaderboardEntries);
		REGISTER_FUNCTION(SteamUserStats, GetLeaderboardEntryCount);
		REGISTER_FUNCTION(SteamUserStats, GetLeaderboardName);
		REGISTER_FUNCTION(SteamUtils, IsAPICallCompleted);

		lua_pushlightuserdata(L, this);
		lua_setglobal(L, "__SteamPlugin");

		#undef REGISTER_FUNCTION

		#define REGISTER_FUNCTION(group, func) \
			LuaWrapper::createSystemFunction(L, #group, #func, &LUA_##func) \

		REGISTER_FUNCTION(SteamUserStats, GetDownloadedLeaderboardEntry);
		REGISTER_FUNCTION(SteamUtils, GetLeaderboardFindResult);
		REGISTER_FUNCTION(SteamUtils, GetLeaderboardScoresDownloaded);

		#undef REGISTER_FUNCTION
	}

	Engine& m_engine;
	HashMap<::uint64, Texture*> m_avatars;
};



LUMIX_PLUGIN_ENTRY(steam)
{
	return LUMIX_NEW(engine.getAllocator(), SteamPlugin)(engine);
}
