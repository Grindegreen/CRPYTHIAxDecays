cmake_minimum_required(VERSION 2.8.12)

set(CMAKE_CXX_STANDARD 11)
set(PLUGINNAME "Decays")
project(${PLUGINNAME} C CXX)

find_package(PythonInterp)
find_package(PythonLibs)
find_package(SWIG REQUIRED)

# get information on crpropa
execute_process(COMMAND ${PYTHON_EXECUTABLE} "${CMAKE_CURRENT_SOURCE_DIR}/findCRPropa.py" swig_interface OUTPUT_VARIABLE crpropaSwigPath)
execute_process(COMMAND ${PYTHON_EXECUTABLE} "${CMAKE_CURRENT_SOURCE_DIR}/findCRPropa.py" install_prefix OUTPUT_VARIABLE crpropaInstallPrefix)
message(STATUS "CRPropa includes : ${crpropaInstallPrefix}")

find_path(CRPROPA_SWIG_INTERFACE_FILES
  crpropa.i
  HINTS crpropa/ share/crpropa python/ ${crpropaSwigPath} 
  )

find_path(CRPROPA_INCLUDES
  CRPropa.h
  HINTS crpropa/ include/crpropa ${crpropaInstallPrefix}/include/
  )

find_library(CRPROPA_LIB NAMES crpropa libcrpropa HINTS crpropa/ lib/crpropa crpropa/lib ${crpropaInstallPrefix}/lib/)

#find_library(PYTHIA8_LIB NAMES pythia8 libpythia8 HINTS pythia8311/ pythia8311/lib/ ${PYTHIA8_LIBRARIES})


# ---- PYTHIA8 discovery ----
# You may pass -DPYTHIA8_DIR=~/Applications/PYTHIA/pythia8315
if(NOT PYTHIA8_DIR AND DEFINED ENV{PYTHIA8})
  set(PYTHIA8_DIR $ENV{PYTHIA8})
endif()

# Try pythia8-config if present
find_program(PYTHIA8_CONFIG NAMES pythia8-config
             HINTS ${PYTHIA8_DIR} $ENV{PYTHIA8} PATH_SUFFIXES bin)

set(PY8_PREFIX "")
set(PY8_CXXFLAGS "")
set(PY8_LDFLAGS "")

if(PYTHIA8_CONFIG)
  execute_process(COMMAND ${PYTHIA8_CONFIG} --prefix
                  OUTPUT_VARIABLE PY8_PREFIX OUTPUT_STRIP_TRAILING_WHITESPACE)
  execute_process(COMMAND ${PYTHIA8_CONFIG} --cxxflags
                  OUTPUT_VARIABLE PY8_CXXFLAGS OUTPUT_STRIP_TRAILING_WHITESPACE)
  execute_process(COMMAND ${PYTHIA8_CONFIG} --ldflags
                  OUTPUT_VARIABLE PY8_LDFLAGS OUTPUT_STRIP_TRAILING_WHITESPACE)
  separate_arguments(PY8_CXXFLAGS)
  separate_arguments(PY8_LDFLAGS)
endif()

# Determine include dir
if(NOT PYTHIA8_INCLUDE_DIR)
  if(PY8_PREFIX)
    set(PYTHIA8_INCLUDE_DIR "${PY8_PREFIX}/include")
  elseif(PYTHIA8_DIR)
    set(PYTHIA8_INCLUDE_DIR "${PYTHIA8_DIR}/include")
  endif()
endif()

# Determine library
if(NOT PYTHIA8_LIBRARY)
  if(PY8_PREFIX)
    find_library(PYTHIA8_LIBRARY NAMES pythia8
                 HINTS "${PY8_PREFIX}/lib" "${PY8_PREFIX}/lib64")
  elseif(PYTHIA8_DIR)
    find_library(PYTHIA8_LIBRARY NAMES pythia8
                 HINTS "${PYTHIA8_DIR}/lib" "${PYTHIA8_DIR}/lib64")
  endif()
endif()

if(NOT EXISTS "${PYTHIA8_INCLUDE_DIR}/Pythia8/Pythia.h")
  message(FATAL_ERROR "Pythia header not found in ${PYTHIA8_INCLUDE_DIR}/Pythia8. \
Pass -DPYTHIA8_DIR=/path/to/pythia or -DPYTHIA8_INCLUDE_DIR=/path/to/pythia/include")
endif()
if(NOT PYTHIA8_LIBRARY)
  message(FATAL_ERROR "libpythia8 not found. Pass -DPYTHIA8_LIBRARY=/path/to/libpythia8.so")
endif()

message(STATUS "Pythia8 include : ${PYTHIA8_INCLUDE_DIR}")
message(STATUS "Pythia8 library : ${PYTHIA8_LIBRARY}")
if(PYTHIA8_CONFIG)
  message(STATUS "Pythia8 cxxflags: ${PY8_CXXFLAGS}")
  message(STATUS "Pythia8 ldflags : ${PY8_LDFLAGS}")
endif()

# Make sure headers are on the path
include_directories(${PYTHIA8_INCLUDE_DIR})

message(STATUS "CRPropa SWIG interface files: ${CRPROPA_SWIG_INTERFACE_FILES}")
message(STATUS "CRPropa includes : ${CRPROPA_INCLUDES}")
message(STATUS "CRPropa lib: ${CRPROPA_LIB}")

# swig plugin
set_source_files_properties(${CMAKE_CURRENT_BINARY_DIR}/${PLUGINNAME}_wrap.cxx PROPERTIES GENERATED true )
add_custom_command( OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/${PLUGINNAME}_wrap.cxx
  COMMAND swig -builtin -c++ -python ${PY3} -I${CRPROPA_SWIG_INTERFACE_FILES} -I${CRPROPA_INCLUDES} -I${CMAKE_SOURCE_DIR}/include -I${PYTHON_INCLUDE_DIRS} -I${PYTHIA8_INCLUDE_DIR} ${CRPROPA_SWIG_DEFINES} -dirprot -o ${CMAKE_CURRENT_BINARY_DIR}/${PLUGINNAME}_wrap.cxx -outdir ${CMAKE_CURRENT_BINARY_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/${PLUGINNAME}.i)

include_directories(include ${CMAKE_CURRENT_BINARY_DIR}/include ${CRPROPA_INCLUDES} ${PYTHON_INCLUDE_DIRS} ${CMAKE_CURRENT_SOURCE_DIR})

# --- sources ---
add_library(${PLUGINNAME} MODULE
  ${CMAKE_CURRENT_BINARY_DIR}/${PLUGINNAME}_wrap.cxx
  ${CMAKE_CURRENT_SOURCE_DIR}/Decays.cc
  # Optional: add this tiny file to force the linker to keep libpythia8
  # ${CMAKE_CURRENT_SOURCE_DIR}/force_pythia_link.cc
)

# Keep a hard dependency on libpythia8 and other libs
target_link_libraries(${PLUGINNAME} PRIVATE
  ${CRPROPA_LIB}
  ${PYTHON_LIBRARIES}
  ${PYTHIA8_LIBRARY}
)

# (CMake 2.8.12 doesn’t have target_link_options reliably; use LINK_FLAGS)
# Ensure --no-as-needed so libpythia8 stays in NEEDED
set_property(TARGET ${PLUGINNAME} APPEND_STRING PROPERTY LINK_FLAGS " -Wl,--no-as-needed")

# Python extension naming
set_target_properties(${PLUGINNAME} PROPERTIES PREFIX "" OUTPUT_NAME "_${PLUGINNAME}")

# RPATH so Python can find libpythia8 at import-time
get_filename_component(PY8_LIBDIR "${PYTHIA8_LIBRARY}" DIRECTORY)
set_target_properties(${PLUGINNAME} PROPERTIES
  BUILD_RPATH "${PY8_LIBDIR}"
  INSTALL_RPATH "${PY8_LIBDIR}"
)

set_target_properties(${PLUGINNAME} PROPERTIES PREFIX "")
set_target_properties(${PLUGINNAME} PROPERTIES OUTPUT_NAME "_${PLUGINNAME}")

execute_process(
        COMMAND ${PYTHON_EXECUTABLE} -c "import sys; from distutils import sysconfig; sys.stdout.write(
sysconfig.get_python_lib(1,0,prefix='${CMAKE_INSTALL_PREFIX}'))"
        OUTPUT_VARIABLE PYTHON_SITE_PACKAGES
        OUTPUT_STRIP_TRAILING_WHITESPACE
        )

# installation 
install(DIRECTORY "${CMAKE_SOURCE_DIR}/python/${PLUGINNAME}" DESTINATION "${PYTHON_SITE_PACKAGES}")
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/${PLUGINNAME}.py" DESTINATION "${PYTHON_SITE_PACKAGES}/${PLUGINNAME}")
install(TARGETS ${PLUGINNAME} LIBRARY DESTINATION "${PYTHON_SITE_PACKAGES}/${PLUGINNAME}")


