cmake_minimum_required(VERSION 3.1.2)

project(LumixEngine)

set(LUMIX_ROOT_PATH ${PROJECT_SOURCE_DIR}/../)
set(SRC_PATH ${LUMIX_ROOT_PATH}src/)
set(EXTERNAL_PATH ${LUMIX_ROOT_PATH}external/)

find_package(Qt5Core REQUIRED)
find_package(Qt5Gui REQUIRED)
find_package(Qt5Widgets REQUIRED)
find_package(Qt5UiTools REQUIRED)


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

target_link_libraries(engine psapi)
target_link_libraries(engine ${EXTERNAL_PATH}lua/lib/debug/lua.lib)

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
target_link_libraries(renderer ${EXTERNAL_PATH}lua/lib/debug/lua.lib)
target_link_libraries(renderer ${EXTERNAL_PATH}bgfx/lib/bgfx${CMAKE_CFG_INTDIR}.lib)


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
target_link_libraries(physics editor)
target_link_libraries(physics studio_lib)
target_link_libraries(physics Qt5::Core)
target_link_libraries(physics Qt5::Gui)
target_link_libraries(physics Qt5::Widgets)
target_link_libraries(physics ${EXTERNAL_PATH}physx/lib/x86/PhysXVisualDebuggerSDKCHECKED.lib)
target_link_libraries(physics ${EXTERNAL_PATH}physx/lib/x86/PhysX3CHECKED_x86.lib)
target_link_libraries(physics ${EXTERNAL_PATH}physx/lib/x86/PhysX3CommonCHECKED_x86.lib)
target_link_libraries(physics ${EXTERNAL_PATH}physx/lib/x86/PhysX3ExtensionsCHECKED.lib)
target_link_libraries(physics ${EXTERNAL_PATH}physx/lib/x86/PhysX3CharacterKinematicCHECKED_x86.lib)
target_link_libraries(physics ${EXTERNAL_PATH}physx/lib/x86/PhysX3CookingCHECKED_x86.lib)

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

###################### STUDIO_LIB ######################

set(CMAKE_INCLUDE_CURRENT_DIR ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)

file(GLOB STUDIO_ROOT_FILES ${SRC_PATH}studio/*.cpp ${SRC_PATH}studio/*.h)
file(GLOB STUDIO_PROPERTY_VIEW_FILES ${SRC_PATH}studio/property_view/*.cpp ${SRC_PATH}studio/property_view/*.h)
file(GLOB STUDIO_DIALOGS_FILES ${SRC_PATH}studio/dialogs/*.cpp ${SRC_PATH}studio/dialogs/*.h)
file(GLOB STUDIO_SCRIPTS_FILES ${SRC_PATH}studio/scripts/*.cpp ${SRC_PATH}studio/scripts/*.h)
file(GLOB STUDIO_PC_FILES ${SRC_PATH}studio/pc/*.cpp ${SRC_PATH}studio/pc/*.h)
file(GLOB STUDIO_ANIMATION_EDITOR_FILES ${SRC_PATH}studio/animation_editor/*.cpp ${SRC_PATH}studio/animation_editor/*.h)
file(GLOB_RECURSE STUDIO_FORM_FILES ${SRC_PATH}studio/*.ui)
list(REMOVE_ITEM STUDIO_ROOT_FILES ${SRC_PATH}studio/main.cpp)

qt5_wrap_ui(${STUDIO_FORM_FILES})

source_group("" FILES ${STUDIO_ROOT_FILES})
source_group("forms" FILES ${STUDIO_FORM_FILES})
source_group("property_view" FILES ${STUDIO_PROPERTY_VIEW_FILES})
source_group("dialogs" FILES ${STUDIO_DIALOGS_FILES})
source_group("scripts" FILES ${STUDIO_SCRIPTS_FILES})
source_group("pc" FILES ${STUDIO_PC_FILES})
source_group("animation_editor" FILES ${STUDIO_ANIMATION_EDITOR_FILES})

include_directories(${SRC_PATH})
include_directories(${SRC_PATH}studio)
include_directories(${EXTERNAL_PATH}assimp/include)
include_directories(${EXTERNAL_PATH}crunch/include)

add_library(studio_lib SHARED
	${STUDIO_ROOT_FILES}
	${STUDIO_PC_FILES}
	${STUDIO_FORM_FILES}
	${STUDIO_PROPERTY_VIEW_FILES}
	${STUDIO_DIALOGS_FILES}
	${STUDIO_SCRIPTS_FILES}
	${STUDIO_ANIMATION_EDITOR_FILES}
)

target_link_libraries(studio_lib engine)
# target_link_libraries(studio_lib script)
target_link_libraries(studio_lib renderer)
target_link_libraries(studio_lib animation)
target_link_libraries(studio_lib editor)
target_link_libraries(studio_lib Qt5::Core)
target_link_libraries(studio_lib Qt5::Gui)
target_link_libraries(studio_lib Qt5::Widgets)
target_link_libraries(studio_lib Qt5::WinMain)
target_link_libraries(studio_lib optimized ${EXTERNAL_PATH}assimp/lib/win32/assimp-vc120-mt.lib)
target_link_libraries(studio_lib debug ${EXTERNAL_PATH}assimp/lib/win32/assimp-vc120-mtd.lib)
target_link_libraries(studio_lib ${EXTERNAL_PATH}crunch/lib/win32/crnlib${CMAKE_CFG_INTDIR}.lib)


set_target_properties (studio_lib PROPERTIES COMPILE_DEFINITIONS "BUILDING_STUDIO_LIB")

# set_target_properties(studio_lib PROPERTIES LINK_FLAGS "/SUBSYSTEM:WINDOWS")
 

###################### STUDIO ######################

set(CMAKE_INCLUDE_CURRENT_DIR ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)

include_directories(${SRC_PATH})
include_directories(${SRC_PATH}studio)
include_directories(${EXTERNAL_PATH}assimp/include)
include_directories(${EXTERNAL_PATH}crunch/include)

add_executable(studio ${SRC_PATH}studio/main.cpp)

add_dependencies(studio lua_script)

target_link_libraries(studio editor)
target_link_libraries(studio renderer)
target_link_libraries(studio studio_lib)

set_target_properties(studio PROPERTIES LINK_FLAGS "/SUBSYSTEM:WINDOWS")
 


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

file(GLOB RENDER_TEST_FILES ${SRC_PATH}render_test/*.*)

source_group("" FILES ${RENDER_TEST_FILES})

include_directories(${SRC_PATH})
include_directories(${SRC_PATH}render_test/)

add_executable(render_test
	${RENDER_TEST_FILES}
)

set_target_properties(render_test PROPERTIES LINK_FLAGS "/SUBSYSTEM:WINDOWS")
target_link_libraries(render_test engine)
target_link_libraries(render_test renderer)
target_link_libraries(render_test Qt5::Core)
target_link_libraries(render_test Qt5::Gui)
target_link_libraries(render_test Qt5::Widgets)
target_link_libraries(render_test Qt5::WinMain)

###################### SAMPLE APP ######################

file(GLOB SAMPLE_APP_FILES ${SRC_PATH}sample_app/*.cpp ${SRC_PATH}sample_app/*.c ${SRC_PATH}sample_app/*.h)
file(GLOB OCORNUT_IMGUI_FILES ${SRC_PATH}sample_app/ocornut-imgui/*.cpp ${SRC_PATH}sample_app/ocornut-imgui/*.h)
file(GLOB STB_FILES ${SRC_PATH}sample_app/stb/*.cpp ${SRC_PATH}sample_app/stb/*.c ${SRC_PATH}sample_app/stb/*.h)

source_group("" FILES ${SAMPLE_APP_FILES})
source_group("ocornut-imgui" FILES ${OCORNUT_IMGUI_FILES})
source_group("stb" FILES ${STB_FILES})

include_directories(${SRC_PATH})
include_directories(${SRC_PATH}sample_app/)

add_executable(sample_app
	${SAMPLE_APP_FILES}
	${OCORNUT_IMGUI_FILES}
	${STB_FILES}
)

set_target_properties(sample_app PROPERTIES LINK_FLAGS "/SUBSYSTEM:WINDOWS")
target_link_libraries(sample_app engine)
target_link_libraries(sample_app renderer)
target_link_libraries(sample_app editor)


###################### LUA_SCRIPT ######################

file(GLOB LUA_SCRIPT_ROOT_FILES ${SRC_PATH}lua_script/*.cpp ${SRC_PATH}lua_script/*.h)

include_directories(${EXTERNAL_PATH}lua/include)
source_group("" FILES ${LUA_SCRIPT_ROOT_FILES})

include_directories(${SRC_PATH})

add_library(lua_script SHARED
	${LUA_SCRIPT_ROOT_FILES}
)

set_target_properties (lua_script PROPERTIES COMPILE_DEFINITIONS "BUILDING_LUA_SCRIPT")

target_link_libraries(lua_script Qt5::Core)
target_link_libraries(lua_script Qt5::Gui)
target_link_libraries(lua_script Qt5::Widgets)
target_link_libraries(lua_script Qt5::UiTools)

target_link_libraries(lua_script ${EXTERNAL_PATH}lua/lib/debug/lua.lib)
target_link_libraries(lua_script engine)
target_link_libraries(lua_script renderer)
target_link_libraries(lua_script editor)
target_link_libraries(lua_script studio_lib)


###################### COPY_DLLS ######################

if (NOT DEFINED ENV{QTDIR})
	message(WARNING $ENV{QTDIR})
endif()

add_custom_command(TARGET studio POST_BUILD        
    COMMAND ${CMAKE_COMMAND} -E copy_if_different  
        "${EXTERNAL_PATH}assimp/dll/win32/assimp-vc120-mtd.dll"
        $<TARGET_FILE_DIR:studio>)

add_custom_command(TARGET studio POST_BUILD        
    COMMAND ${CMAKE_COMMAND} -E copy_if_different  
        "${EXTERNAL_PATH}physx/dll/x86/PhysX3CommonCHECKED_x86.dll"
        $<TARGET_FILE_DIR:studio>)

add_custom_command(TARGET studio POST_BUILD        
    COMMAND ${CMAKE_COMMAND} -E copy_if_different  
        "${EXTERNAL_PATH}physx/dll/x86/PhysX3CookingCHECKED_x86.dll"
        $<TARGET_FILE_DIR:studio>)

add_custom_command(TARGET studio POST_BUILD        
    COMMAND ${CMAKE_COMMAND} -E copy_if_different  
        "${EXTERNAL_PATH}physx/dll/x86/PhysX3CharacterKinematicCHECKED_x86.dll"
        $<TARGET_FILE_DIR:studio>)

add_custom_command(TARGET studio POST_BUILD        
    COMMAND ${CMAKE_COMMAND} -E copy_if_different  
        "${EXTERNAL_PATH}physx/dll/x86/PhysX3CHECKED_x86.dll"
        $<TARGET_FILE_DIR:studio>)