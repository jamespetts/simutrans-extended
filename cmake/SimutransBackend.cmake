#
# This file is part of the Simutrans-Extended project under the Artistic License.
# (see LICENSE.txt)
#


if (SDL2_FOUND)
	list(APPEND AVAILABLE_BACKENDS "sdl2")
	mark_as_advanced(SDL2_DIR)
endif (SDL2_FOUND)

if (WIN32)
	list(APPEND AVAILABLE_BACKENDS "gdi")
endif ()

# SDL3 as an additional platform layer, with the EXISTING software renderer on
# top. Appended after the others on purpose: the default backend is the FIRST
# entry of this list, so sdl3 can never become the default by accident. It has
# to be asked for with -DSIMUTRANS_BACKEND=sdl3.
if (SDL3_FOUND)
	list(APPEND AVAILABLE_BACKENDS "sdl3")
	mark_as_advanced(SDL3_DIR)
endif ()

list(APPEND AVAILABLE_BACKENDS "none")

string(REGEX MATCH "^[^;][^;]*" FIRST_BACKEND "${AVAILABLE_BACKENDS}")
set(SIMUTRANS_BACKEND "${FIRST_BACKEND}" CACHE STRING "Graphics backend")
set_property(CACHE SIMUTRANS_BACKEND PROPERTY STRINGS ${AVAILABLE_BACKENDS})
