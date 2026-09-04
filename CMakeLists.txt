cmake_minimum_required(VERSION 3.12)

project(obs-face-tracker VERSION 0.9.1)
set(PLUGIN_AUTHOR "Norihiro Kamae")
set(MACOS_BUNDLEID "net.nagater.obs-face-tracker")
set(MACOS_PACKAGE_UUID "A2EB2F73-9828-40C4-A307-F47282BB8D3F")
set(MACOS_INSTALLER_UUID "652CF739-5A6F-4DAC-9611-B068FAC15BF3")
set(PLUGIN_URL "https://obsproject.com/forum/resources/face-tracker.1294/")
set(LINUX_MAINTAINER_EMAIL "norihiro@nagater.net")

option(WITH_PTZ_TCP "Enable to connect PTZ camera through TCP socket" ON)
option(WITH_DLIB_SUBMODULE "Link dlib from submodule" ON)
option(ENABLE_MONITOR_USER "Enable monitor source for user" OFF)
option(ENABLE_DEBUG_DATA "Enable property to save error and control data" OFF)
option(WITH_DOCK "Enable dock" ON)
option(ENABLE_DATAGEN "Enable generating data" OFF)

set(CMAKE_PREFIX_PATH "${QTDIR}")

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(plugin_additional_libs)
set(plugin_additional_incs)

if (WITH_PTZ_TCP)
	set(CMAKE_POSITION_INDEPENDENT_CODE true)
	set(WITH_VISCA_SERIAL OFF)
	add_definitions("-DVISCA_API= ")
	add_subdirectory(libvisca/visca)
	set(plugin_additional_libs ${plugin_additional_libs} visca)
	set(plugin_additional_incs ${plugin_additional_incs} libvisca/visca)
endif()

find_package(libobs REQUIRED)
find_package(obs-frontend-api REQUIRED)
include(cmake/ObsPluginHelpers.cmake)
find_qt(VERSION ${QT_VERSION} COMPONENTS Widgets Core Gui)

if (WITH_DLIB_SUBMODULE)
	set(CMAKE_POSITION_INDEPENDENT_CODE True)
	set(DLIB_NO_GUI_SUPPORT ON)
	set(DLIB_PNG_SUPPORT OFF)
	set(DLIB_GIF_SUPPORT OFF)
	set(DLIB_USE_FFMPEG OFF)
	set(DLIB_JPEG_SUPPORT OFF)
	set(DLIB_LINK_WITH_SQLITE3 OFF)
	set(DLIB_WEBP_SUPPORT OFF)
	set(DLIB_JXL_SUPPORT OFF)
	set(DLIB_TEST_COMPILE_ALL_SOURCE_CPP OFF)
	add_subdirectory(dlib)
else()
	find_package(dlib REQUIRED)
	add_library(dlib ALIAS dlib::dlib)
endif()

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)

configure_file(
	src/plugin-macros.h.in
	plugin-macros.generated.h
)
configure_file(
	installer/installer-Windows.iss.in
	installer-Windows.generated.iss
)
configure_file(
	ci/ci_includes.sh.in
	ci/ci_includes.generated.sh
)
configure_file(
	ci/ci_includes.cmd.in
	ci/ci_includes.generated.cmd
)

if(OS_WINDOWS)
	if (MSVC)
		add_definitions(/MP /d2FH4-)
		add_definitions("-D_USE_MATH_DEFINES")
		add_definitions("-D_CRT_SECURE_NO_WARNINGS")
		add_definitions("-D_DISABLE_CONSTEXPR_MUTEX_CONSTRUCTOR")
	endif()
endif()

# ==============================================================================
# 1. PLUGIN: obs-face-tracker (Digital Face Tracker Filter & Dock)
# ==============================================================================
set(FACE_TRACKER_SOURCES
	src/module-face-tracker.c
	src/face-tracker.cpp
	src/source_list.cc
	src/face-tracker-preset.cpp
	src/face-tracker-manager.cpp
	src/face-tracker-monitor.cpp
	src/face-detector-base.cpp
	src/face-detector-dlib-hog.cpp
	src/face-detector-dlib-cnn.cpp
	src/face-tracker-base.cpp
	src/face-tracker-dlib.cpp
	src/texture-object.cpp
	src/helper.cpp
)

if (WITH_DOCK)
	list(APPEND FACE_TRACKER_SOURCES
		ui/face-tracker-dock.cpp
		ui/face-tracker-widget.cpp
	)
endif()

add_library(obs-face-tracker MODULE ${FACE_TRACKER_SOURCES})
target_link_libraries(obs-face-tracker
	OBS::libobs
	OBS::obs-frontend-api
	dlib
	Qt::Gui Qt::Widgets Qt::GuiPrivate
)
target_include_directories(obs-face-tracker PRIVATE
	${CMAKE_CURRENT_BINARY_DIR}
	${CMAKE_CURRENT_SOURCE_DIR}/src
	${CMAKE_CURRENT_SOURCE_DIR}/dlib
)
if(OS_WINDOWS)
	target_link_libraries(obs-face-tracker OBS::w32-pthreads)
endif()
setup_plugin_target(obs-face-tracker)


# ==============================================================================
# 2. PLUGIN: obs-ptz-tracker (PTZ Hardware Camera Tracking & Control)
# ==============================================================================
set(PTZ_TRACKER_SOURCES
	src/module-ptz-tracker.c
	src/face-tracker-ptz.cpp
	src/face-tracker-preset.cpp
	src/face-tracker-manager.cpp
	src/face-detector-base.cpp
	src/face-detector-dlib-hog.cpp
	src/face-detector-dlib-cnn.cpp
	src/face-tracker-base.cpp
	src/face-tracker-dlib.cpp
	src/texture-object.cpp
	src/helper.cpp
	src/source_list.cc
	src/ptz-backend.cpp
	src/obsptz-backend.cpp
	src/dummy-backend.cpp
)

if (WITH_PTZ_TCP)
	list(APPEND PTZ_TRACKER_SOURCES src/libvisca-thread.cpp)
endif()

add_library(obs-ptz-tracker MODULE ${PTZ_TRACKER_SOURCES})
target_link_libraries(obs-ptz-tracker
	OBS::libobs
	OBS::obs-frontend-api
	dlib
	${plugin_additional_libs}
	Qt::Gui Qt::Widgets
)
target_include_directories(obs-ptz-tracker PRIVATE
	${CMAKE_CURRENT_BINARY_DIR}
	${CMAKE_CURRENT_SOURCE_DIR}/src
	${CMAKE_CURRENT_SOURCE_DIR}/dlib
	${plugin_additional_incs}
)
if(OS_WINDOWS)
	target_link_libraries(obs-ptz-tracker OBS::w32-pthreads)
endif()
setup_plugin_target(obs-ptz-tracker)


# ==============================================================================
# 3. PLUGIN: obs-joystick-controller (Gamepad PTZ & Mesa de Corte)
# ==============================================================================
set(JOYSTICK_CONTROLLER_SOURCES
	src/module-joystick-controller.c
	src/gamepad-controller.cpp
	ui/gamepad-dock.cpp
)

add_library(obs-joystick-controller MODULE ${JOYSTICK_CONTROLLER_SOURCES})
target_link_libraries(obs-joystick-controller
	OBS::libobs
	OBS::obs-frontend-api
	Qt::Gui Qt::Widgets Qt::GuiPrivate
)
target_include_directories(obs-joystick-controller PRIVATE
	${CMAKE_CURRENT_BINARY_DIR}
	${CMAKE_CURRENT_SOURCE_DIR}/src
)

if(OS_WINDOWS)
	target_include_directories(obs-joystick-controller PRIVATE
		${CMAKE_CURRENT_SOURCE_DIR}/deps/SDL2/include
	)
	target_link_directories(obs-joystick-controller PRIVATE
		${CMAKE_CURRENT_SOURCE_DIR}/deps/SDL2/lib/x64
	)
	target_link_libraries(obs-joystick-controller OBS::w32-pthreads SDL2)
endif()
setup_plugin_target(obs-joystick-controller)


# ==============================================================================
# Common flags and options
# ==============================================================================
if(OS_LINUX)
	foreach(_tgt obs-face-tracker obs-ptz-tracker obs-joystick-controller)
		target_compile_options(${_tgt}
			PRIVATE
			-Wall
			-Wextra
			-Wunused-function
			-Werror
			-Wno-error=deprecated-declarations
		)
		target_link_options(${_tgt} PRIVATE "-Wl,-z,defs")
	endforeach()
endif()

if(APPLE)
	set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -stdlib=libc++ -fvisibility=default")
	foreach(_tgt obs-face-tracker obs-ptz-tracker obs-joystick-controller)
		set_target_properties(${_tgt} PROPERTIES PREFIX "")
		set(MACOSX_PLUGIN_GUI_IDENTIFIER "${MACOS_BUNDLEID}")
		set(MACOSX_PLUGIN_BUNDLE_VERSION "${CMAKE_PROJECT_VERSION}")
		set(MACOSX_PLUGIN_SHORT_VERSION_STRING "1")
	endforeach()
endif()

configure_file(installer/installer-macOS.pkgproj.in installer-macOS.generated.pkgproj)

if(ENABLE_DATAGEN)
	add_executable(face-detector-dlib-hog-datagen
		src/face-detector-dlib-hog-datagen.cpp
	)
	target_include_directories(face-detector-dlib-hog-datagen PRIVATE
		${CMAKE_CURRENT_BINARY_DIR}
	)
	target_link_libraries(face-detector-dlib-hog-datagen
		dlib
	)
endif()
