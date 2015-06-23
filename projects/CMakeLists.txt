cmake_minimum_required(VERSION 3.2)

project(LumixEngine)

set(LUMIX_ROOT_PATH ${PROJECT_SOURCE_DIR}/../)
set(SRC_PATH ${LUMIX_ROOT_PATH}src/)
set(EXTERNAL_PATH ${LUMIX_ROOT_PATH}external/)


###################### CORE ######################

file(GLOB CORE_ROOT_FILES ${SRC_PATH}core/*.cpp ${SRC_PATH}core/*.h)
file(GLOB CORE_MTJD_FILES ${SRC_PATH}core/MTJD/*.cpp ${SRC_PATH}core/MTJD/*.h)
file(GLOB CORE_MT_FILES ${SRC_PATH}core/MT/*.cpp ${SRC_PATH}core/MT/*.h ${SRC_PATH}core/MT/pc/*.cpp ${SRC_PATH}core/MT/pc/*.h)
file(GLOB CORE_FS_FILES ${SRC_PATH}core/FS/*.cpp ${SRC_PATH}core/FS/*.h)
file(GLOB CORE_NET_FILES ${SRC_PATH}core/Net/*.cpp ${SRC_PATH}core/Net/*.h ${SRC_PATH}core/Net/pc/*.cpp ${SRC_PATH}core/Net/pc/*.h)
file(GLOB CORE_PC_FILES ${SRC_PATH}core/PC/*.cpp ${SRC_PATH}core/PC/*.h)
file(GLOB CORE_DEBUG_FILES ${SRC_PATH}debug/*.cpp ${SRC_PATH}debug/*.h ${SRC_PATH}debug/pc/*.cpp ${SRC_PATH}debug/pc/*.h)

source_group(debug FILES ${CORE_DEBUG_FILES})
source_group(fs FILES ${CORE_FS_FILES})
source_group(mt FILES ${CORE_MT_FILES})
source_group(mtjd FILES ${CORE_MTJD_FILES})
source_group(net FILES ${CORE_NET_FILES})
source_group("" FILES ${CORE_ROOT_FILES})
source_group("" FILES ${CORE_PC_FILES})


include_directories(${SRC_PATH})

add_library(core SHARED
	${CORE_ROOT_FILES}
	${CORE_FS_FILES}
	${CORE_MT_FILES}
	${CORE_NET_FILES}
	${CORE_MTJD_FILES}
	${CORE_PC_FILES}
	${CORE_DEBUG_FILES}
)

set_target_properties (core PROPERTIES COMPILE_DEFINITIONS "BUILDING_CORE")


###################### ENGINE ######################

file(GLOB ENGINE_ROOT_FILES ${SRC_PATH}engine/*.cpp ${SRC_PATH}engine/*.h)
file(GLOB ENGINE_UNIVERSE_FILES ${SRC_PATH}universe/*.cpp ${SRC_PATH}universe/*.h)
file(GLOB ENGINE_GRAPHICS_FILES ${SRC_PATH}graphics/*.cpp ${SRC_PATH}graphics/*.h)
file(GLOB ENGINE_EDITOR_FILES ${SRC_PATH}editor/*.cpp ${SRC_PATH}editor/*.h)

source_group(editor FILES ${ENGINE_EDITOR_FILES})
source_group(universe FILES ${ENGINE_UNIVERSE_FILES})
source_group(graphics FILES ${ENGINE_GRAPHICS_FILES})
source_group("" FILES ${ENGINE_ROOT_FILES})

include_directories(${SRC_PATH})
include_directories(${SRC_PATH}studio)
include_directories(${EXTERNAL_PATH}bgfx/include)
include_directories(${EXTERNAL_PATH}lua/include)

add_library(engine SHARED
	${ENGINE_ROOT_FILES}
	${ENGINE_UNIVERSE_FILES}
	${ENGINE_GRAPHICS_FILES}
	${ENGINE_EDITOR_FILES}
)

set_target_properties (engine PROPERTIES COMPILE_DEFINITIONS "BUILDING_ENGINE")

target_link_libraries(engine core)
target_link_libraries(engine psapi)
target_link_libraries(engine ${EXTERNAL_PATH}lua/lib/debug/lua.lib)
target_link_libraries(engine ${EXTERNAL_PATH}bgfx/lib/bgfx${CMAKE_CFG_INTDIR}.lib)


###################### PHYSICS ######################

file(GLOB PHYSICS_ROOT_FILES ${SRC_PATH}physics/*.cpp ${SRC_PATH}physics/*.h)

source_group("" FILES ${PHYSICS_ROOT_FILES})

include_directories(${SRC_PATH})
include_directories(${EXTERNAL_PATH}physx/include)

add_library(physics SHARED
	${PHYSICS_ROOT_FILES}
)

set_target_properties (physics PROPERTIES COMPILE_DEFINITIONS "BUILDING_PHYSICS")

target_link_libraries(physics core)
target_link_libraries(physics engine)
target_link_libraries(physics ${EXTERNAL_PATH}physx/lib/x86/PhysXVisualDebuggerSDKCHECKED.lib)
target_link_libraries(physics ${EXTERNAL_PATH}physx/lib/x86/PhysX3CHECKED_x86.lib)
target_link_libraries(physics ${EXTERNAL_PATH}physx/lib/x86/PhysX3CommonCHECKED_x86.lib)
target_link_libraries(physics ${EXTERNAL_PATH}physx/lib/x86/PhysX3ExtensionsCHECKED.lib)
target_link_libraries(physics ${EXTERNAL_PATH}physx/lib/x86/PhysX3CharacterKinematicCHECKED_x86.lib)
target_link_libraries(physics ${EXTERNAL_PATH}physx/lib/x86/PhysX3CookingCHECKED_x86.lib)

###################### SCRIPT ######################

file(GLOB SCRIPT_ROOT_FILES ${SRC_PATH}script/*.cpp ${SRC_PATH}script/*.h)

source_group("" FILES ${SCRIPT_ROOT_FILES})

include_directories(${SRC_PATH})

add_library(script SHARED
	${SCRIPT_ROOT_FILES}
)

set_target_properties (script PROPERTIES COMPILE_DEFINITIONS "BUILDING_SCRIPT")

target_link_libraries(script core)
target_link_libraries(script engine)


###################### ANIMATION ######################

file(GLOB ANIMATION_ROOT_FILES ${SRC_PATH}animation/*.cpp ${SRC_PATH}animation/*.h)

source_group("" FILES ${ANIMATION_ROOT_FILES})

include_directories(${SRC_PATH})

add_library(animation SHARED
	${ANIMATION_ROOT_FILES}
)

set_target_properties (animation PROPERTIES COMPILE_DEFINITIONS "BUILDING_ANIMATION")

target_link_libraries(animation core)
target_link_libraries(animation engine)


###################### STUDIO ######################

set(CMAKE_INCLUDE_CURRENT_DIR ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)

find_package(Qt5Core REQUIRED)
find_package(Qt5Gui REQUIRED)
find_package(Qt5Widgets REQUIRED)

file(GLOB STUDIO_ROOT_FILES ${SRC_PATH}studio/*.cpp ${SRC_PATH}studio/*.h)
file(GLOB STUDIO_PROPERTY_VIEW_FILES ${SRC_PATH}studio/property_view/*.cpp ${SRC_PATH}studio/property_view/*.h)
file(GLOB STUDIO_DIALOGS_FILES ${SRC_PATH}studio/dialogs/*.cpp ${SRC_PATH}studio/dialogs/*.h)
file(GLOB STUDIO_SCRIPTS_FILES ${SRC_PATH}studio/scripts/*.cpp ${SRC_PATH}studio/scripts/*.h)
file(GLOB STUDIO_PC_FILES ${SRC_PATH}studio/pc/*.cpp ${SRC_PATH}studio/pc/*.h)
file(GLOB STUDIO_ANIMATION_EDITOR_FILES ${SRC_PATH}studio/animation_editor/*.cpp ${SRC_PATH}studio/animation_editor/*.h)
file(GLOB_RECURSE STUDIO_FORM_FILES ${SRC_PATH}studio/*.ui)

qt5_wrap_ui(${STUDIO_FORM_FILES})

source_group("" FILES ${STUDIO_ROOT_FILES})
source_group("forms" FILES ${STUDIO_FORM_FILES})
source_group("property_view" FILES ${STUDIO_PROPERTY_VIEW_FILES})
source_group("dialogs" FILES ${STUDIO_DIALOGS_FILES})
source_group("scripts" FILES ${STUDIO_SCRIPTS_FILES})
source_group("pc" FILES ${STUDIO_PC_FILES})
source_group("animation_editor" FILES ${STUDIO_ANIMATION_EDITOR_FILES})

include_directories(${SRC_PATH})
include_directories(${EXTERNAL_PATH}assimp/include)
include_directories(${EXTERNAL_PATH}crunch/include)

add_executable(studio
	${STUDIO_ROOT_FILES}
	${STUDIO_PC_FILES}
	${STUDIO_FORM_FILES}
	${STUDIO_PROPERTY_VIEW_FILES}
	${STUDIO_DIALOGS_FILES}
	${STUDIO_SCRIPTS_FILES}
	${STUDIO_ANIMATION_EDITOR_FILES}
)

target_link_libraries(studio core)
target_link_libraries(studio engine)
target_link_libraries(studio script)
target_link_libraries(studio physics)
target_link_libraries(studio animation)
target_link_libraries(studio Qt5::Core)
target_link_libraries(studio Qt5::Gui)
target_link_libraries(studio Qt5::Widgets)
target_link_libraries(studio Qt5::WinMain)
target_link_libraries(studio ${EXTERNAL_PATH}assimp/lib/win32/assimp-vc120-mt.lib)
target_link_libraries(studio ${EXTERNAL_PATH}crunch/lib/win32/crnlib${CMAKE_CFG_INTDIR}.lib)
set_target_properties(studio PROPERTIES LINK_FLAGS "/SUBSYSTEM:WINDOWS")
 

###################### COPY_DLLS ######################

if (NOT DEFINED ENV{QTDIR})
	message(WARNING $ENV{QTDIR})
endif()

add_custom_command(TARGET studio POST_BUILD        
    COMMAND ${CMAKE_COMMAND} -E copy_if_different  
        "${EXTERNAL_PATH}assimp/dll/win32/assimp-vc120-mt.dll"
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