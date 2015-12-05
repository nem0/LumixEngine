cmake_minimum_required(VERSION 3.1.2)


string(FIND ${CMAKE_GENERATOR} "Win64" POS_X64)
if(POS_X64 GREATER -1)
	set(IS64 "TRUE")
	set(PLATFORM_DIR "win64")
else(POS_X64 GREATER -1)
	set(IS64 "FALSE")
	set(PLATFORM_DIR "win32")
endif(POS_X64 GREATER -1)

MESSAGE( STATUS "Generator: " ${CMAKE_GENERATOR} )
MESSAGE( STATUS "64bit: " ${IS64} )

project(LumixEngine)

SET(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /WX")
set(LUMIX_ROOT_PATH ${PROJECT_SOURCE_DIR}/../)
set(SRC_PATH ${LUMIX_ROOT_PATH}src/)
set(EXTERNAL_PATH ${LUMIX_ROOT_PATH}external/)
set(CONFIG_DIR $<$<CONFIG:Debug>:Debug>$<$<NOT:$<CONFIG:Debug>>:Release>)

###################### ENGINE ######################

set(ENGINE_ROOT_PATH ${SRC_PATH}engine/)

file(GLOB CORE_ROOT_FILES ${ENGINE_ROOT_PATH}core/*.cpp ${ENGINE_ROOT_PATH}core/*.h)
file(GLOB CORE_MTJD_FILES ${ENGINE_ROOT_PATH}core/MTJD/*.cpp ${ENGINE_ROOT_PATH}core/MTJD/*.h)
file(GLOB CORE_MT_FILES ${ENGINE_ROOT_PATH}core/MT/*.cpp ${ENGINE_ROOT_PATH}core/MT/*.h ${ENGINE_ROOT_PATH}core/MT/pc/*.cpp ${ENGINE_ROOT_PATH}core/MT/pc/*.h)
file(GLOB CORE_FS_FILES ${ENGINE_ROOT_PATH}core/FS/*.cpp ${ENGINE_ROOT_PATH}core/FS/*.h ${ENGINE_ROOT_PATH}core/FS/pc/*.cpp)
file(GLOB CORE_NET_FILES ${ENGINE_ROOT_PATH}core/Net/*.cpp ${ENGINE_ROOT_PATH}core/Net/*.h ${ENGINE_ROOT_PATH}core/Net/pc/*.cpp ${ENGINE_ROOT_PATH}core/Net/pc/*.h)
file(GLOB CORE_PC_FILES ${ENGINE_ROOT_PATH}core/PC/*.cpp ${ENGINE_ROOT_PATH}core/PC/*.h)
file(GLOB CORE_DEBUG_FILES ${ENGINE_ROOT_PATH}debug/*.cpp ${ENGINE_ROOT_PATH}debug/*.h ${ENGINE_ROOT_PATH}debug/pc/*.cpp ${ENGINE_ROOT_PATH}debug/pc/*.h)
file(GLOB ENGINE_FILES ${ENGINE_ROOT_PATH}*.cpp ${ENGINE_ROOT_PATH}*.h)
file(GLOB UNIVERSE_FILES ${ENGINE_ROOT_PATH}universe/*.cpp ${ENGINE_ROOT_PATH}universe/*.h)


source_group(core FILES ${CORE_ROOT_FILES})
source_group(core FILES ${CORE_PC_FILES})
source_group(core\\debug FILES ${CORE_DEBUG_FILES})
source_group(core\\fs FILES ${CORE_FS_FILES})
source_group(core\\mt FILES ${CORE_MT_FILES})
source_group(core\\mtjd FILES ${CORE_MTJD_FILES})
source_group(core\\net FILES ${CORE_NET_FILES})
source_group(universe FILES ${UNIVERSE_FILES})
source_group("" FILES ${ENGINE_FILES})

include_directories(${SRC_PATH})
include_directories(${ENGINE_ROOT_PATH})
include_directories(${EXTERNAL_PATH}lua/include)

add_library(engine SHARED
	${CORE_ROOT_FILES}
	${CORE_FS_FILES}
	${CORE_MT_FILES}
	${CORE_NET_FILES}
	${CORE_MTJD_FILES}
	${CORE_PC_FILES}
	${CORE_DEBUG_FILES}
	${ENGINE_FILES}
	${UNIVERSE_FILES}
)

set_target_properties (engine PROPERTIES COMPILE_DEFINITIONS "BUILDING_ENGINE")
set_target_properties (engine PROPERTIES COMPILE_DEFINITIONS "BUILDING_ENGINE")


target_link_libraries(engine psapi)
target_link_libraries(engine ${EXTERNAL_PATH}lua/lib/vs2013/${PLATFORM_DIR}/${CONFIG_DIR}/lua.lib)

###################### RENDERER ######################

file(GLOB RENDERER_ROOT_FILES ${SRC_PATH}renderer/*.cpp ${SRC_PATH}renderer/*.h)

source_group("" FILES ${RENDERER_ROOT_FILES})

include_directories(${SRC_PATH})
include_directories(${ENGINE_ROOT_PATH})
include_directories(${EXTERNAL_PATH}bgfx/include)
include_directories(${EXTERNAL_PATH}lua/include)

add_library(renderer SHARED
	${RENDERER_ROOT_FILES}
)

set_target_properties (renderer PROPERTIES COMPILE_DEFINITIONS "BUILDING_RENDERER")

target_link_libraries(renderer engine)

target_link_libraries(renderer ${EXTERNAL_PATH}lua/lib/vs2013/${PLATFORM_DIR}/${CONFIG_DIR}/lua.lib)
target_link_libraries(renderer ${EXTERNAL_PATH}bgfx/lib/vs2013/${PLATFORM_DIR}/bgfx${CONFIG_DIR}.lib)

###################### AUDIO ######################

set(AUDIO_ROOT_PATH ${SRC_PATH}audio/)

file(GLOB AUDIO_ROOT_FILES ${AUDIO_ROOT_PATH}*.cpp ${AUDIO_ROOT_PATH}pc/*.cpp ${AUDIO_ROOT_PATH}*.h ${AUDIO_ROOT_PATH}*.c)

source_group("" FILES ${AUDIO_ROOT_FILES})

include_directories(${SRC_PATH})
include_directories(${ENGINE_ROOT_PATH})
include_directories(${AUDIO_ROOT_PATH})

add_library(audio SHARED
	${AUDIO_ROOT_FILES}
)

set_target_properties (audio PROPERTIES COMPILE_DEFINITIONS "BUILDING_AUDIO")

target_link_libraries(audio engine)
target_link_libraries(audio dxguid.lib)


###################### PHYSICS ######################

file(GLOB PHYSICS_ROOT_FILES ${SRC_PATH}physics/*.cpp ${SRC_PATH}physics/*.h)

source_group("" FILES ${PHYSICS_ROOT_FILES})

include_directories(${SRC_PATH})
include_directories(${EXTERNAL_PATH}physx/include)

add_library(physics SHARED
	${PHYSICS_ROOT_FILES}
)

set_target_properties (physics PROPERTIES COMPILE_DEFINITIONS "BUILDING_PHYSICS")

target_link_libraries(physics engine)
target_link_libraries(physics renderer)
target_link_libraries(physics optimized ${EXTERNAL_PATH}physx/lib/${PLATFORM_DIR}/PhysXVisualDebuggerSDKCHECKED.lib)
target_link_libraries(physics debug ${EXTERNAL_PATH}physx/lib/${PLATFORM_DIR}/PhysXVisualDebuggerSDKDEBUG.lib)
if(${IS64})
	target_link_libraries(physics ${EXTERNAL_PATH}physx/lib/${PLATFORM_DIR}/PhysX3CHECKED_x64.lib)
	target_link_libraries(physics ${EXTERNAL_PATH}physx/lib/${PLATFORM_DIR}/PhysX3CommonCHECKED_x64.lib)
	target_link_libraries(physics optimized ${EXTERNAL_PATH}physx/lib/${PLATFORM_DIR}/PhysX3ExtensionsCHECKED.lib)
	target_link_libraries(physics debug ${EXTERNAL_PATH}physx/lib/${PLATFORM_DIR}/PhysX3ExtensionsDEBUG.lib)
	target_link_libraries(physics ${EXTERNAL_PATH}physx/lib/${PLATFORM_DIR}/PhysX3CharacterKinematicCHECKED_x64.lib)
	target_link_libraries(physics ${EXTERNAL_PATH}physx/lib/${PLATFORM_DIR}/PhysX3CookingCHECKED_x64.lib)
else(${IS64})
	target_link_libraries(physics ${EXTERNAL_PATH}physx/lib/${PLATFORM_DIR}/PhysX3CHECKED_x86.lib)
	target_link_libraries(physics ${EXTERNAL_PATH}physx/lib/${PLATFORM_DIR}/PhysX3CommonCHECKED_x86.lib)
	target_link_libraries(physics optimized ${EXTERNAL_PATH}physx/lib/${PLATFORM_DIR}/PhysX3ExtensionsCHECKED.lib)
	target_link_libraries(physics debug ${EXTERNAL_PATH}physx/lib/${PLATFORM_DIR}/PhysX3ExtensionsDEBUG.lib)
	target_link_libraries(physics ${EXTERNAL_PATH}physx/lib/${PLATFORM_DIR}/PhysX3CharacterKinematicCHECKED_x86.lib)
	target_link_libraries(physics ${EXTERNAL_PATH}physx/lib/${PLATFORM_DIR}/PhysX3CookingCHECKED_x86.lib)
endif(${IS64})

###################### SCRIPT ######################

#[[
file(GLOB SCRIPT_ROOT_FILES ${SRC_PATH}script/*.cpp ${SRC_PATH}script/*.h)

source_group("" FILES ${SCRIPT_ROOT_FILES})

include_directories(${SRC_PATH})

add_library(script SHARED
	${SCRIPT_ROOT_FILES}
)

set_target_properties (script PROPERTIES COMPILE_DEFINITIONS "BUILDING_SCRIPT")


target_link_libraries(script engine)
]]

###################### ANIMATION ######################

file(GLOB ANIMATION_ROOT_FILES ${SRC_PATH}animation/*.cpp ${SRC_PATH}animation/*.h)

source_group("" FILES ${ANIMATION_ROOT_FILES})

include_directories(${SRC_PATH})

add_library(animation SHARED
	${ANIMATION_ROOT_FILES}
)

set_target_properties (animation PROPERTIES COMPILE_DEFINITIONS "BUILDING_ANIMATION")


target_link_libraries(animation engine)
target_link_libraries(animation renderer)

###################### EDITOR ######################

file(GLOB EDITOR_ROOT_FILES ${SRC_PATH}editor/*.cpp ${SRC_PATH}editor/*.h)

include_directories(${EXTERNAL_PATH}lua/include)
source_group("" FILES ${EDITOR_ROOT_FILES})

include_directories(${SRC_PATH})

add_library(editor SHARED
	${EDITOR_ROOT_FILES}
)

set_target_properties (editor PROPERTIES COMPILE_DEFINITIONS "BUILDING_EDITOR")


target_link_libraries(editor engine)
target_link_libraries(editor renderer)


###################### UNIT TESTS ######################

file(GLOB UNIT_TESTS_ROOT_FILES ${SRC_PATH}unit_tests/*.cpp ${SRC_PATH}unit_tests/*.h)
file(GLOB UNIT_TESTS_CORE_FILES ${SRC_PATH}unit_tests/core/*.cpp ${SRC_PATH}unit_tests/core/*.h)
file(GLOB UNIT_TESTS_ENGINE_FILES ${SRC_PATH}unit_tests/engine/*.cpp ${SRC_PATH}unit_tests/engine/*.h)
file(GLOB UNIT_TESTS_GRAPHICS_FILES ${SRC_PATH}unit_tests/graphics/*.cpp ${SRC_PATH}unit_tests/graphics/*.h)
file(GLOB UNIT_TESTS_SUITE_FILES ${SRC_PATH}unit_tests/suite/*.cpp ${SRC_PATH}unit_tests/suite/*.h)


source_group("" FILES ${UNIT_TESTS_ROOT_FILES})
source_group(core FILES ${UNIT_TESTS_CORE_FILES})
source_group(engine FILES ${UNIT_TESTS_ENGINE_FILES})
source_group(graphics FILES ${UNIT_TESTS_GRAPHICS_FILES})
source_group(suite FILES ${UNIT_TESTS_SUITE_FILES})

include_directories(${SRC_PATH})

add_executable(unit_tests
	${UNIT_TESTS_ROOT_FILES}
	${UNIT_TESTS_CORE_FILES}
	${UNIT_TESTS_ENGINE_FILES}
	${UNIT_TESTS_GRAPHICS_FILES}
	${UNIT_TESTS_SUITE_FILES}
)

set_target_properties(unit_tests PROPERTIES LINK_FLAGS "/SUBSYSTEM:CONSOLE")


target_link_libraries(unit_tests engine)
target_link_libraries(unit_tests animation)


###################### RENDER TEST ######################

### file(GLOB RENDER_TEST_FILES ${SRC_PATH}render_test/*.*)
### 
### source_group("" FILES ${RENDER_TEST_FILES})
### 
### include_directories(${SRC_PATH})
### include_directories(${SRC_PATH}render_test/)
### 
### add_executable(render_test
### 	${RENDER_TEST_FILES}
### )
### 
### set_target_properties(render_test PROPERTIES LINK_FLAGS "/SUBSYSTEM:WINDOWS")
### target_link_libraries(render_test engine)
### target_link_libraries(render_test renderer)
### target_link_libraries(render_test Qt5::Core)
### target_link_libraries(render_test Qt5::Gui)
### target_link_libraries(render_test Qt5::Widgets)
### target_link_libraries(render_test Qt5::WinMain)

###################### STUDIO ######################

file(GLOB STUDIO_FILES ${SRC_PATH}studio/*.cpp ${SRC_PATH}studio/pc/*.cpp ${SRC_PATH}studio/*.c ${SRC_PATH}studio/*.h ${SRC_PATH}studio/*.inl)
file(GLOB OCORNUT_IMGUI_FILES ${SRC_PATH}studio/ocornut-imgui/*.cpp ${SRC_PATH}studio/ocornut-imgui/*.h)
file(GLOB STB_FILES ${SRC_PATH}studio/stb/*.cpp ${SRC_PATH}studio/stb/*.c ${SRC_PATH}studio/stb/*.h)

source_group("" FILES ${STUDIO_FILES})
source_group("ocornut-imgui" FILES ${OCORNUT_IMGUI_FILES})
source_group("stb" FILES ${STB_FILES})

include_directories(${SRC_PATH})
include_directories(${SRC_PATH}studio/)
include_directories(${EXTERNAL_PATH}assimp/include)
include_directories(${EXTERNAL_PATH}crunch/include)

add_executable(studio
	${STUDIO_FILES}
	${OCORNUT_IMGUI_FILES}
	${STB_FILES}
)

add_dependencies(studio audio engine animation physics renderer lua_script editor)

set_target_properties(studio PROPERTIES LINK_FLAGS "/SUBSYSTEM:WINDOWS")


target_link_libraries(studio engine)
target_link_libraries(studio audio)
target_link_libraries(studio renderer)
target_link_libraries(studio editor)

target_link_libraries(studio ${EXTERNAL_PATH}assimp/lib/vs2013/${PLATFORM_DIR}/${CONFIG_DIR}/assimp.lib)
target_link_libraries(studio ${EXTERNAL_PATH}crunch/lib/vs2013/${PLATFORM_DIR}/${CONFIG_DIR}/crnlib.lib)

target_link_libraries(studio winmm.lib)



###################### LUA_SCRIPT ######################

file(GLOB LUA_SCRIPT_ROOT_FILES ${SRC_PATH}lua_script/*.cpp ${SRC_PATH}lua_script/*.h)

include_directories(${EXTERNAL_PATH}lua/include)
source_group("" FILES ${LUA_SCRIPT_ROOT_FILES})

include_directories(${SRC_PATH})

add_library(lua_script SHARED
	${LUA_SCRIPT_ROOT_FILES}
)

set_target_properties (lua_script PROPERTIES COMPILE_DEFINITIONS "BUILDING_LUA_SCRIPT")


target_link_libraries(lua_script ${EXTERNAL_PATH}lua/lib/vs2013/${PLATFORM_DIR}/${CONFIG_DIR}/lua.lib)
target_link_libraries(lua_script engine)
target_link_libraries(lua_script renderer)


###################### COPY_DLLS ######################


add_custom_command(TARGET studio POST_BUILD        
    COMMAND ${CMAKE_COMMAND} -E copy_if_different  
        "${EXTERNAL_PATH}assimp/dll/vs2013/${PLATFORM_DIR}/$<$<CONFIG:Debug>:Debug>$<$<NOT:$<CONFIG:Debug>>:Release>/assimp.dll"
        $<TARGET_FILE_DIR:studio>)

if(${IS64})
	
	add_custom_command(TARGET studio POST_BUILD        
		COMMAND ${CMAKE_COMMAND} -E copy_if_different  
			"${EXTERNAL_PATH}physx/dll/${PLATFORM_DIR}/nvToolsExt64_1.dll"
			$<TARGET_FILE_DIR:studio>)

	add_custom_command(TARGET studio POST_BUILD        
		COMMAND ${CMAKE_COMMAND} -E copy_if_different  
			"${EXTERNAL_PATH}physx/dll/${PLATFORM_DIR}/PhysX3CommonCHECKED_x64.dll"
			$<TARGET_FILE_DIR:studio>)

	add_custom_command(TARGET studio POST_BUILD        
		COMMAND ${CMAKE_COMMAND} -E copy_if_different  
			"${EXTERNAL_PATH}physx/dll/${PLATFORM_DIR}/PhysX3CookingCHECKED_x64.dll"
			$<TARGET_FILE_DIR:studio>)

	add_custom_command(TARGET studio POST_BUILD        
		COMMAND ${CMAKE_COMMAND} -E copy_if_different  
			"${EXTERNAL_PATH}physx/dll/${PLATFORM_DIR}/PhysX3CharacterKinematicCHECKED_x64.dll"
			$<TARGET_FILE_DIR:studio>)

	add_custom_command(TARGET studio POST_BUILD        
		COMMAND ${CMAKE_COMMAND} -E copy_if_different  
			"${EXTERNAL_PATH}physx/dll/${PLATFORM_DIR}/PhysX3CHECKED_x64.dll"
			$<TARGET_FILE_DIR:studio>)

else(${IS64})

	add_custom_command(TARGET studio POST_BUILD        
		COMMAND ${CMAKE_COMMAND} -E copy_if_different  
			"${EXTERNAL_PATH}physx/dll/${PLATFORM_DIR}/nvToolsExt32_1.dll"
			$<TARGET_FILE_DIR:studio>)

	add_custom_command(TARGET studio POST_BUILD        
		COMMAND ${CMAKE_COMMAND} -E copy_if_different  
			"${EXTERNAL_PATH}physx/dll/${PLATFORM_DIR}/PhysX3CommonCHECKED_x86.dll"
			$<TARGET_FILE_DIR:studio>)

	add_custom_command(TARGET studio POST_BUILD        
		COMMAND ${CMAKE_COMMAND} -E copy_if_different  
			"${EXTERNAL_PATH}physx/dll/${PLATFORM_DIR}/PhysX3CookingCHECKED_x86.dll"
			$<TARGET_FILE_DIR:studio>)

	add_custom_command(TARGET studio POST_BUILD        
		COMMAND ${CMAKE_COMMAND} -E copy_if_different  
			"${EXTERNAL_PATH}physx/dll/${PLATFORM_DIR}/PhysX3CharacterKinematicCHECKED_x86.dll"
			$<TARGET_FILE_DIR:studio>)

	add_custom_command(TARGET studio POST_BUILD        
		COMMAND ${CMAKE_COMMAND} -E copy_if_different  
			"${EXTERNAL_PATH}physx/dll/${PLATFORM_DIR}/PhysX3CHECKED_x86.dll"
			$<TARGET_FILE_DIR:studio>)

endif(${IS64})


