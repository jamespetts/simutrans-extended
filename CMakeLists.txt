#
# This file is part of the Simutrans-Extended project under the Artistic License.
# (see LICENSE.txt)
#

cmake_minimum_required(VERSION 3.8)

# Disable in-source builds
if (CMAKE_SOURCE_DIR STREQUAL CMAKE_BINARY_DIR)
	message(FATAL_ERROR "Building Simutrans in-source is not supported. "
		"Please delete ${CMAKE_SOURCE_DIR}/CMakeCache.txt and configure in a different "
		"(preferrably empty) directory.")
endif (CMAKE_SOURCE_DIR STREQUAL CMAKE_BINARY_DIR)

set(CMAKE_WARN_DEPRECATED ON)
list(APPEND CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/cmake/")

include(SimutransVcpkgTriplet)

project(simutrans-extended LANGUAGES C CXX)

include(SimutransCommitInfo)

# Force C++11 everywhere
set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED YES)
set(CMAKE_CXX_EXTENSIONS OFF)

include(TestBigEndian)
TEST_BIG_ENDIAN(SIMUTRANS_BIG_ENDIAN)

if (MINGW)
	set(CMAKE_FIND_LIBRARY_SUFFIXES ".a" ON)
	set(CMAKE_EXE_LINKER_FLAGS "-static -static-libgcc -static-libstdc++")
endif ()

if (APPLE)
	set(CMAKE_PREFIX_PATH "/usr/local/opt/readline")
	link_directories(/usr/local/lib)
	set(CMAKE_EXE_LINKER_FLAGS "-framework Cocoa")
endif ()

#
# Dependencies
#
find_package(CCache)
find_package(ZLIB REQUIRED)
find_package(BZip2 REQUIRED)
find_package(PNG REQUIRED)
find_package(MiniUPNP)
if (MSVC)
	find_package(ZSTD)
	find_package(SDL2)
	find_package(Freetype)
	find_package(FluidSynth)
else ()
	find_package(PkgConfig MODULE REQUIRED)
	pkg_check_modules(ZSTD IMPORTED_TARGET libzstd)
	pkg_check_modules(SDL2 IMPORTED_TARGET sdl2)
	pkg_check_modules(Freetype IMPORTED_TARGET freetype2)
	pkg_check_modules(FluidSynth IMPORTED_TARGET fluidsynth>=2.1.0)
endif ()

set(CMAKE_THREAD_PREFER_PTHREAD ON)
find_package(Threads)

#
# Configuration options
#
include(SimutransBackend) # Select backend (SDL2 / GDI)
include(SimutransCompileOptions)

# CMake does not fill in CMAKE_BUILD_TYPE automatically.
if (NOT MSVC)
    if (CMAKE_BUILD_TYPE STREQUAL "")
        set(CMAKE_BUILD_TYPE "RelWithDebInfo" CACHE STRING "Build type. Valid values are Debug Release MinSizeRel RelWithDebInfo" FORCE)
    endif ()
    set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS Debug Release MinSizeRel RelWithDebInfo)
endif (NOT MSVC)


#
# sources
#
add_executable(simutrans-extended)
if (NOT CMAKE_BUILD_TYPE STREQUAL "Debug")
	set_target_properties(simutrans-extended PROPERTIES WIN32_EXECUTABLE 1)
endif ()

include(SimutransSourceList)


#
# output directory
#
file(MAKE_DIRECTORY ${CMAKE_BINARY_DIR}/simutrans)
set_target_properties( simutrans-extended PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/simutrans/$<0:> )


#
# Libraries
#

target_link_libraries(simutrans-extended PRIVATE PNG::PNG)


if (WIN32)
	target_sources(simutrans-extended PRIVATE simres.rc)
	target_sources(simutrans-extended PRIVATE sys/clipboard_w32.cc)
	target_link_libraries(simutrans-extended PRIVATE ws2_32 winmm)

	# Backup sound and music routines if the selected backend does not provide any routines
	if (MINGW)
		set(BACKUP_SOUND sound/win32_sound.cc)
	else ()
		set(BACKUP_SOUND sound/win32_sound_xa.cc)
	endif ()
	set(BACKUP_MUSIC music/w32_midi.cc)
else ()
	if (SIMUTRANS_BACKEND STREQUAL "sdl2")
		target_sources(simutrans-extended PRIVATE sys/clipboard_s2.cc)
	else ()
		target_sources(simutrans-extended PRIVATE sys/clipboard_internal.cc)
	endif ()
	if (APPLE)
		#Darwin kernel version is used to identify macOS version (16->10.12/Sierra)
		if (CMAKE_HOST_SYSTEM_VERSION VERSION_GREATER_EQUAL 16) #AVF
			set(BACKUP_SOUND sound/AVF_core-audio_sound.mm)
			#AVF MIDI currently crash Simutrans
			#set(BACKUP_MUSIC music/AVF_core-audio_midi.mm)
			set(BACKUP_MUSIC music/no_midi.cc)
		else () #QTKit
			set(BACKUP_SOUND sound/core-audio_sound.mm)
			set(BACKUP_MUSIC music/core-audio_midi.mm)
		endif ()
	else ()
		set(BACKUP_SOUND sound/no_sound.cc)
		set(BACKUP_MUSIC music/no_midi.cc)
	endif ()
endif ()

if (SIMUTRANS_MULTI_THREAD)
	target_compile_definitions(simutrans-extended PRIVATE MULTI_THREAD=1)
	target_link_libraries(simutrans-extended PRIVATE Threads::Threads)
endif (SIMUTRANS_MULTI_THREAD)

if (SIMUTRANS_BACKEND STREQUAL "sdl2")
	target_sources(simutrans-extended PRIVATE display/simgraph16.cc sys/simsys_s2.cc sound/sdl2_sound.cc)
	target_include_directories(simutrans-extended PRIVATE ${SDL2_INCLUDE_DIRS})
	if (MINGW)
		target_link_libraries(simutrans-extended PRIVATE ${SDL2_STATIC_LIBRARIES})
	elseif (APPLE)
		target_link_libraries(simutrans-extended PRIVATE ${SDL2_LIBRARIES})
	elseif (MSVC)
		target_link_libraries(simutrans-extended PRIVATE SDL2::SDL2)
	else ()
		target_link_libraries(simutrans-extended PRIVATE PkgConfig::SDL2)
	endif ()
	target_compile_definitions(simutrans-extended PRIVATE COLOUR_DEPTH=16)

elseif (SIMUTRANS_BACKEND STREQUAL "gdi")
	target_sources(simutrans-extended PRIVATE display/simgraph16.cc sys/simsys_w.cc ${BACKUP_SOUND} )
	target_link_libraries(simutrans-extended PRIVATE imm32 xaudio2_8)
	target_compile_definitions(simutrans-extended PRIVATE COLOUR_DEPTH=16)

else ()
	if (NOT SIMUTRANS_BACKEND STREQUAL "none")
		message(WARNING "Unknown backend '${SIMUTRANS_BACKEND}', falling back to headless compilation")
	endif ()

	target_sources(simutrans-extended PRIVATE display/simgraph0.cc sys/simsys_posix.cc sound/no_sound.cc music/no_midi.cc)
	target_compile_definitions(simutrans-extended PRIVATE COLOUR_DEPTH=0)
endif ()


#
# Link dependencies
#
target_link_libraries(simutrans-extended PRIVATE ZLIB::ZLIB)
target_link_libraries(simutrans-extended PRIVATE BZip2::BZip2)

if (SIMUTRANS_USE_FREETYPE)
	target_include_directories(simutrans-extended PRIVATE ${Freetype_INCLUDE_DIRS})
	if (MINGW)
		target_link_libraries(simutrans-extended PRIVATE ${Freetype_STATIC_LIBRARIES})
	elseif (APPLE)
		target_link_libraries(simutrans-extended PRIVATE ${Freetype_LIBRARIES})
	elseif (MSVC)
		target_link_libraries(simutrans-extended PRIVATE Freetype::Freetype)
	else ()
		target_link_libraries(simutrans-extended PRIVATE PkgConfig::Freetype)
	endif ()
	target_compile_definitions(simutrans-extended PRIVATE USE_FREETYPE=1)
endif (SIMUTRANS_USE_FREETYPE)

if (SIMUTRANS_USE_UPNP)
	target_compile_definitions(simutrans-extended PRIVATE USE_UPNP=1)
	target_link_libraries(simutrans-extended PRIVATE MiniUPNP::MiniUPNP)
endif (SIMUTRANS_USE_UPNP)

if (SIMUTRANS_USE_ZSTD)
	target_sources(simutrans-extended PRIVATE io/rdwr/zstd_file_rdwr_stream.cc)
	target_include_directories(simutrans-extended PRIVATE ${ZSTD_INCLUDE_DIRS})
	target_compile_definitions(simutrans-extended PRIVATE USE_ZSTD=1)
	if (MSVC)
		target_link_libraries(simutrans-extended PRIVATE zstd::libzstd_static)
	elseif (MINGW)
		# only 64 bit cmake, 32 bit is broken
		target_link_libraries(simutrans-extended PRIVATE -lzstd)
	else()
		target_link_libraries(simutrans-extended PRIVATE PkgConfig::ZSTD)
	endif ()
endif (SIMUTRANS_USE_ZSTD)

# only consider FluidSynth for SDL2 and GDI backends
if ((SIMUTRANS_BACKEND STREQUAL "sdl2") OR (SIMUTRANS_BACKEND STREQUAL "gdi"))
	if (SIMUTRANS_USE_FLUIDSYNTH_MIDI)
		target_sources(simutrans-extended PRIVATE music/fluidsynth.cc gui/loadsoundfont_frame.cc)
		target_compile_definitions(simutrans-extended PRIVATE USE_FLUIDSYNTH_MIDI=1)
		if (MINGW)
			# not working because fluidsynth doesn't set static libraries for pkgconf
			#target_link_libraries(simutrans-extended PRIVATE ${FluidSynth_STATIC_LIBRARIES})
			# manually linking them instead, until it is fixed
			target_link_libraries(simutrans-extended PRIVATE -lfluidsynth -lharfbuzz -lgraphite2 -lglib-2.0 -lintl -liconv -ldsound -lole32 -lws2_32 -lgomp -lksuser)
		elseif (MSVC)
			target_link_libraries(simutrans-extended PRIVATE ${FluidSynth_LIBRARIES})
		else ()
			target_link_libraries(simutrans-extended PRIVATE PkgConfig::FluidSynth)
		endif (MINGW)
	else ()
		target_sources( simutrans-extended PRIVATE ${BACKUP_MUSIC} )
	endif (SIMUTRANS_USE_FLUIDSYNTH_MIDI)
endif ((SIMUTRANS_BACKEND STREQUAL "sdl2") OR (SIMUTRANS_BACKEND STREQUAL "gdi"))

#
# compile options
#

if (MSVC)
	target_link_options(simutrans-extended PRIVATE /INCREMENTAL:NO /NODEFAULTLIB:LIBCMTD /MANIFEST:NO /LARGEADDRESSAWARE)
endif ()

if (SIMUTRANS_BIG_ENDIAN)
	target_compile_definitions(simutrans-extended PRIVATE SIM_BIG_ENDIAN=1)
endif ()

if (SIMUTRANS_VALGRIND_SUPPORT)
	add_definitions(-DUSE_VALGRIND_MEMCHECK=1)
endif ()

if (SIMUTRANS_ENABLE_PROFILING)
	add_definitions(-DPROFILE=1)
endif ()

target_compile_options(simutrans-extended PRIVATE ${SIMUTRANS_COMMON_COMPILE_OPTIONS})

target_compile_definitions(simutrans-extended PRIVATE $<IF:$<CONFIG:Debug>,DEBUG=1,>)
target_compile_definitions(simutrans-extended PRIVATE $<IF:$<CONFIG:Release>,NDEBUG=1,>)

target_compile_definitions(simutrans-extended PRIVATE MSG_LEVEL=${SIMUTRANS_MSG_LEVEL})

if (SIMUTRANS_WITH_REVISION)
	target_compile_definitions(simutrans-extended PRIVATE REVISION=${SIMUTRANS_SHA_SHORT})
endif ()

if (SIMUTRANS_AUTOJOIN_PUBLIC)
	target_compile_definitions(simutrans-extended PRIVATE AUTOJOIN_PUBLIC=1)
endif ()

if (SIMUTRANS_ENABLE_WATERWAY_SIGNS)
	target_compile_definitions(simutrans-extended PRIVATE ENABLE_WATERWAY_SIGNS=1)
endif ()

if (SIMUTRANS_USE_SYSLOG)
	target_compile_definitions(simutrans-extended PRIVATE SYSLOG=1)
endif ()

if (SIMUTRANS_DEBUG_FLUSH_BUFFER)
	target_compile_definitions(simutrans-extended PRIVATE DEBUG_FLUSH_BUFFER=1)
endif ()

if (SIMUTRANS_USE_IP4_ONLY)
	target_compile_definitions(simutrans-extended PRIVATE USE_IP4_ONLY=1)
endif ()

if (SIMUTRANS_STEAM_BUILT)
	target_compile_definitions(simutrans-extended PRIVATE STEAM_BUILT=1)
endif ()

if (SIMUTRANS_DISABLE_RANDOMNESS)
	target_compile_definitions(simutrans-extended PRIVATE DISABLE_RANDOMNESS=1)
endif ()

if (SIMUTRANS_DEBUG_SIMRAND)
	target_compile_definitions(simutrans-extended PRIVATE DEBUG_SIMRAND_CALLS=1)
endif ()

if (SIMUTRANS_HEAVY_MODE)
	target_compile_definitions(simutrans PRIVATE HEAVY_MODE=${SIMUTRANS_HEAVY_MODE})
	target_sources(simutrans PRIVATE io/rdwr/adler32_stream.cc)
endif ()

if (APPLE)
	target_compile_definitions(simutrans-extended PRIVATE ALT_SDL_DIR=1)
endif (APPLE)


#
# Installation
#
install(TARGETS simutrans-extended RUNTIME DESTINATION simutrans)

install(DIRECTORY "${CMAKE_SOURCE_DIR}/simutrans/" DESTINATION "simutrans")

install(FILES get_pak.sh DESTINATION "simutrans"
	PERMISSIONS
		OWNER_READ OWNER_WRITE OWNER_EXECUTE
		GROUP_READ GROUP_EXECUTE
		WORLD_READ WORLD_EXECUTE
)


#
# Nettool/Makeobj
#
add_subdirectory(makeobj)
add_subdirectory(nettools)


#
# Packaging
#
include(SimutransPack)
