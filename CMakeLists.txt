cmake_minimum_required(VERSION 3.14)

# choose your plugin name
set(pluginName "Decays")

# set the version to whatever you want
project(${pluginName}
	LANGUAGES CXX
)



###############################################################################
#                   Compiler- and system-specific options                     #
###############################################################################

# C++ should be at least 17
set(CMAKE_CXX_STANDARD 17)

# set default build-type to release to enable performance improvements
if(NOT CMAKE_BUILD_TYPE)
	set(CMAKE_BUILD_TYPE Release)
endif(NOT CMAKE_BUILD_TYPE)
message(STATUS "Build Type: ${CMAKE_BUILD_TYPE}")

# set other needed compiler flags:
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fdiagnostics-color=always")

# enable extra warnings on debug builds
set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -Wextra")

# find cmake modules
set(CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake" ${CMAKE_MODULE_PATH})

# compiler-specific flags for Linux and macOS
if(CMAKE_COMPILER_IS_GNUCXX AND NOT APPLE)
	set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} -Wl,--as-needed")
	set(CMAKE_MODULE_LINKER_FLAGS "${CMAKE_MODULE_LINKER_FLAGS} -Wl,--as-needed")
	message(STATUS "Use --as-needed linker flags!")
endif(CMAKE_COMPILER_IS_GNUCXX AND NOT APPLE)

# RPATH handling (Linux and macOS):
#   The plugin (and its Python module) must find libcrpropa and libpythia8, whose
#   install names are @rpath/libcrpropa and @rpath/libpythia8.
#   They must also find their own libraries at runtime.
#   Here we:
#   * keep the linker's build rpath so binaries run from the build tree, and
#   * add both the plugin's own install lib dir and the directories of all linked
#     libraries (CRPropa and Pythia8 included) to the install rpath.
set(CMAKE_MACOSX_RPATH On)
set(CMAKE_SKIP_BUILD_RPATH False)
set(CMAKE_BUILD_WITH_INSTALL_RPATH False)
set(CMAKE_INSTALL_RPATH_USE_LINK_PATH True)

# display compiler information
message(STATUS "C++ compiler: ${CMAKE_CXX_COMPILER} (version ${CMAKE_CXX_COMPILER_VERSION})")
message(STATUS "C++ compilation flags: ${CMAKE_CXX_FLAGS}")

# general options
option(ENABLE_PYTHON "Create Python bindings with SWIG" On)
option(USE_CRPROPA_BUILTIN "Use builtin CRPropa instead of an installed CRPropa" Off)
option(ENABLE_TESTING "Build tests and enable test target" On)
option(ENABLE_PYBIND11_STUBGEN "Generate Pybind11 stubs" Off)


# This token means "relative to the loaded binary" on each platform.
# It should work for unix-based platforms (Linux, MacOS).
if(APPLE)
	set(originToken "@loader_path")
else()
	set(originToken "$ORIGIN")
endif(APPLE)

# search next to the binary and in the plugin's own install lib directory
set(CMAKE_INSTALL_RPATH "${originToken};${CMAKE_INSTALL_PREFIX}/lib")
message(STATUS "Install RPATH: ${CMAKE_INSTALL_RPATH} (+ linked library dirs)")


###############################################################################
#                   Compilation and installation of plugin                    #
###############################################################################

# finding Python
if(ENABLE_PYTHON)
	set(Python_FIND_VIRTUALENV FIRST)
	set(Python_FIND_STRATEGY LOCATION)
	find_package(Python 3 REQUIRED COMPONENTS Interpreter Development.Module NumPy)

	if(Python_FOUND)
		message(STATUS "Python version: ${Python_VERSION}")
		message(STATUS "Python executable: ${Python_EXECUTABLE}")
		message(STATUS "Python headers: ${Python_INCLUDE_DIRS}")
		message(STATUS "Python library: ${Python_LIBRARY}")
		message(STATUS "Python site-packages: ${Python_SITELIB}")
	endif(Python_FOUND)

	# find SWIG
	find_package(SWIG REQUIRED)
	include(UseSWIG)

	# only support versions of SWIG higher than 4.0
	if(SWIG_VERSION VERSION_LESS 4.0)
		message(STATUS "SWIG version too old (${SWIG_VERSION} < 4.0). Code will not be compiled with Python support.")
		set(SWIG_FOUND False)
	endif(SWIG_VERSION VERSION_LESS 4.0)

	if(SWIG_FOUND)
		message(STATUS "SWIG installation directory: ${SWIG_DIR}")
		message(STATUS "SWIG executable: ${SWIG_EXECUTABLE}")
	else()
		message(STATUS "SWIG: NOT FOUND (cannot build with Python bindings)")
	endif(SWIG_FOUND)

	# find NumPy
	if(Python_NumPy_FOUND)
		include_directories(${Python_NumPy_INCLUDE_DIRS})
		message(STATUS "NumPy headers: ${Python_NumPy_INCLUDE_DIRS} (version ${Python_NumPy_VERSION})")
	else()
		message(STATUS "NumPy: NOT FOUND.")
	endif(Python_NumPy_FOUND)

	# use Python_INSTALL_PACKAGE_DIR if provided; otherwise, install in Python_SITELIB
	set(Python_INSTALL_PACKAGE_DIR "${Python_SITELIB}" CACHE PATH "folder in which the python package is installed")
	message(STATUS "  package install directory: ${Python_INSTALL_PACKAGE_DIR}")

endif(ENABLE_PYTHON)


# add other extra libraries by appending to these variables:
set(${pluginName}_EXTRA_INCLUDES)
set(${pluginName}_EXTRA_LIBRARIES)
set(${pluginName}_EXTRA_SOURCES)


# ---- CRPropa ----
if(USE_CRPROPA_BUILTIN)
	if(NOT crpropa_POPULATED)
		include(FetchContent)
		FetchContent_Declare(
			crpropa
			GIT_REPOSITORY "https://github.com/CRPropa/CRPropa3.git"
			GIT_TAG master
		)
		list(APPEND CMAKE_MODULE_PATH "${CRPropa_SOURCE_DIR}/cmake")
		FetchContent_MakeAvailable(crpropa)
	endif(NOT crpropa_POPULATED)

	set(CRPropa_SOURCE_DIR "${crpropa_SOURCE_DIR}")
	set(CRPropa_BINARY_DIR "${crpropa_BINARY_DIR}")

	list(APPEND ${pluginName}_EXTRA_INCLUDES ${CRPropa_SOURCE_DIR}/include)
	list(APPEND ${pluginName}_EXTRA_INCLUDES ${CRPropa_BINARY_DIR}/include)
	list(APPEND ${pluginName}_EXTRA_LIBRARIES crpropa)
	set(CRPropa_SWIG_INTERFACE_FILES ${CRPropa_SOURCE_DIR}/python)
else()
	# Find an installed CRPropa using cmake/FindCRPropa.cmake (found via CMAKE_MODULE_PATH set above).
	# Every location can be overridden manually:
	# - `CRPropa_INSTALL_PREFIX`: the prefix where CRPropa is installed (e.g. /user/CRPropa3/build)
	# - `CRPropa_INCLUDE_DIR`: the directory containing CRPropa headers
	# - `CRPropa_LIBRARY`: the CRPropa library
	# - `CRPropa_SWIG_PATH`: the path to CRPropa SWIG interface files
	find_package(CRPropa REQUIRED)

	list(APPEND ${pluginName}_EXTRA_INCLUDES ${CRPropa_INCLUDE_DIR})
	list(APPEND ${pluginName}_EXTRA_LIBRARIES ${CRPropa_LIBRARY})
	set(CRPropa_SWIG_INTERFACE_FILES ${CRPropa_SWIG_PATH})
endif(USE_CRPROPA_BUILTIN)


# ---- Pythia8 ----
# You may pass -DPYTHIA8_DIR=/path/to/pythia8 or set the PYTHIA8 environment variable.
if(NOT PYTHIA8_DIR AND DEFINED ENV{PYTHIA8})
	set(PYTHIA8_DIR $ENV{PYTHIA8})
endif()

find_program(PYTHIA8_CONFIG NAMES pythia8-config
	HINTS ${PYTHIA8_DIR} $ENV{PYTHIA8} PATH_SUFFIXES bin)

set(PY8_PREFIX "")
if(PYTHIA8_CONFIG)
	execute_process(COMMAND ${PYTHIA8_CONFIG} --prefix
		OUTPUT_VARIABLE PY8_PREFIX OUTPUT_STRIP_TRAILING_WHITESPACE)
endif(PYTHIA8_CONFIG)

if(NOT PYTHIA8_INCLUDE_DIR)
	if(PY8_PREFIX)
		set(PYTHIA8_INCLUDE_DIR "${PY8_PREFIX}/include")
	elseif(PYTHIA8_DIR)
		set(PYTHIA8_INCLUDE_DIR "${PYTHIA8_DIR}/include")
	endif()
endif(NOT PYTHIA8_INCLUDE_DIR)

if(NOT PYTHIA8_LIBRARY)
	if(PY8_PREFIX)
		find_library(PYTHIA8_LIBRARY NAMES pythia8 HINTS "${PY8_PREFIX}/lib" "${PY8_PREFIX}/lib64")
	elseif(PYTHIA8_DIR)
		find_library(PYTHIA8_LIBRARY NAMES pythia8 HINTS "${PYTHIA8_DIR}/lib" "${PYTHIA8_DIR}/lib64")
	endif()
endif(NOT PYTHIA8_LIBRARY)

if(NOT EXISTS "${PYTHIA8_INCLUDE_DIR}/Pythia8/Pythia.h")
	message(FATAL_ERROR "Pythia header not found in ${PYTHIA8_INCLUDE_DIR}/Pythia8. \
Pass -DPYTHIA8_DIR=/path/to/pythia or -DPYTHIA8_INCLUDE_DIR=/path/to/pythia/include")
endif(NOT EXISTS "${PYTHIA8_INCLUDE_DIR}/Pythia8/Pythia.h")
if(NOT PYTHIA8_LIBRARY)
	message(FATAL_ERROR "libpythia8 not found. Pass -DPYTHIA8_LIBRARY=/path/to/libpythia8.so")
endif(NOT PYTHIA8_LIBRARY)

message(STATUS "Pythia8 include : ${PYTHIA8_INCLUDE_DIR}")
message(STATUS "Pythia8 library : ${PYTHIA8_LIBRARY}")

list(APPEND ${pluginName}_EXTRA_INCLUDES ${PYTHIA8_INCLUDE_DIR})
list(APPEND ${pluginName}_EXTRA_LIBRARIES ${PYTHIA8_LIBRARY})


# Add the source files located in ${CMAKE_CURRENT_SOURCE_DIR}/src
add_library(${pluginName} SHARED
	${CMAKE_CURRENT_SOURCE_DIR}/src/Decays.cc
	${${pluginName}_EXTRA_SOURCES}
)

# the plugin's own headers plus any extra includes (CRPropa, Pythia8)
target_include_directories(${pluginName} PUBLIC
	${CMAKE_CURRENT_SOURCE_DIR}/include
	${${pluginName}_EXTRA_INCLUDES}
)

# link crpropa, pythia8 and other needed libraries
target_link_libraries(${pluginName} ${${pluginName}_EXTRA_LIBRARIES})

# Pythia8 symbols are only reached indirectly (templates/vtables), so --as-needed
# can drop the DT_NEEDED entry for libpythia8 unless we override it for this target.
set_property(TARGET ${pluginName} APPEND_STRING PROPERTY LINK_FLAGS " -Wl,--no-as-needed")

# Python interface, this part should work basically fully automated.
if(ENABLE_PYTHON)
	if(Python_FOUND)
		# use SWIG's builtin (maybe should be abandoned if everything will be tested and working with -builtin)
		option(ENABLE_SWIG_BUILTIN "Use SWIG builtin option" On)

		if(ENABLE_SWIG_BUILTIN)
			list(APPEND ${pluginName}_SWIG_DEFINES "-builtin")
		endif(ENABLE_SWIG_BUILTIN)

		set(${pluginName}_PYTHON_PACKAGE_DIR "${CMAKE_CURRENT_BINARY_DIR}/${pluginName}")
		file(MAKE_DIRECTORY "${${pluginName}_PYTHON_PACKAGE_DIR}")
		file(COPY "${CMAKE_CURRENT_SOURCE_DIR}/python/${pluginName}/" DESTINATION "${${pluginName}_PYTHON_PACKAGE_DIR}")

		set_source_files_properties(${CMAKE_CURRENT_SOURCE_DIR}/python/${pluginName}.i
			PROPERTIES
			CPLUSPLUS ON
			SWIG_MODULE_NAME "${pluginName}"
			SWIG_FLAGS "${${pluginName}_SWIG_DEFINES};-dirprot;${CMAKE_SWIG_FLAGS}"
			OUTPUT_DIR "${${pluginName}_PYTHON_PACKAGE_DIR}"
			INCLUDE_DIRECTORIES "${CMAKE_CURRENT_SOURCE_DIR}/include;${CRPropa_SWIG_INTERFACE_FILES};${${pluginName}_EXTRA_INCLUDES};"
		)

		swig_add_library(${pluginName}-swig TYPE SHARED LANGUAGE python
			SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/python/${pluginName}.i
		)

		target_include_directories(${pluginName}-swig PUBLIC
			${CMAKE_CURRENT_SOURCE_DIR}/include
			${${pluginName}_EXTRA_INCLUDES}
			${Python_INCLUDE_DIRS}
			${Python_NumPy_INCLUDE_DIRS}
			${CRPropa_SWIG_INTERFACE_FILES}
		)

		target_link_libraries(${pluginName}-swig ${pluginName} ${Python_LIBRARIES} Python::Module)

		set_target_properties(${pluginName}-swig PROPERTIES
			PREFIX ""
			OUTPUT_NAME "_${pluginName}"
			LIBRARY_OUTPUT_DIRECTORY "${${pluginName}_PYTHON_PACKAGE_DIR}"
			COMPILE_FLAGS  "-w"
			SUFFIX ".so"
		)

		if(APPLE)
			set_target_properties(${pluginName}-swig PROPERTIES INSTALL_RPATH "@loader_path;${CMAKE_INSTALL_PREFIX}/lib")
			set_property(TARGET ${pluginName}-swig APPEND PROPERTY LINK_FLAGS "-flat_namespace -undefined suppress")
		endif(APPLE)

	endif(Python_FOUND)

endif(ENABLE_PYTHON)


###############################################################################
#                                 Testing                                     #
###############################################################################

# Decays currently only ships a Python smoke test (test/testPlugin.py); add a
# test/testPlugin.cpp + gtest wiring here if C++ unit tests are introduced later.
if(ENABLE_TESTING AND ENABLE_PYTHON AND Python_FOUND)
	enable_testing()
	configure_file(${CMAKE_CURRENT_SOURCE_DIR}/test/testPlugin.py ${CMAKE_CURRENT_BINARY_DIR}/testPlugin.py COPYONLY)
	add_test(NAME testPluginPy COMMAND ${Python_EXECUTABLE} ${CMAKE_CURRENT_BINARY_DIR}/testPlugin.py)
endif(ENABLE_TESTING AND ENABLE_PYTHON AND Python_FOUND)


###############################################################################
#                                Installation                                 #
###############################################################################

install(TARGETS ${pluginName} DESTINATION "${CMAKE_INSTALL_PREFIX}/lib")
install(DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/include/" DESTINATION "${CMAKE_INSTALL_PREFIX}/include" FILES_MATCHING PATTERN "*.h")
if(ENABLE_PYTHON AND Python_FOUND)
	install(DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/python/${pluginName}" DESTINATION "${Python_INSTALL_PACKAGE_DIR}")
	install(FILES "${${pluginName}_PYTHON_PACKAGE_DIR}/${pluginName}.py" DESTINATION "${Python_INSTALL_PACKAGE_DIR}/${pluginName}")
	install(TARGETS ${pluginName}-swig LIBRARY DESTINATION "${Python_INSTALL_PACKAGE_DIR}/${pluginName}")
	install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/python/${pluginName}.i" DESTINATION ${CMAKE_INSTALL_PREFIX}/share/${pluginName}/swig_interface)
endif(ENABLE_PYTHON AND Python_FOUND)


if(ENABLE_PYBIND11_STUBGEN AND ENABLE_PYTHON AND Python_FOUND)
	find_program(PYBIND11_STUBGEN_EXECUTABLE NAMES "pybind11-stubgen")
	if(PYBIND11_STUBGEN_EXECUTABLE)
		message(STATUS "Found pybind11-stubgen at ${PYBIND11_STUBGEN_EXECUTABLE}")
		install(CODE "execute_process(COMMAND ${PYBIND11_STUBGEN_EXECUTABLE} --ignore-all-errors -o ${Python_SITELIB} ${pluginName})")
	endif(PYBIND11_STUBGEN_EXECUTABLE)
endif(ENABLE_PYBIND11_STUBGEN AND ENABLE_PYTHON AND Python_FOUND)
