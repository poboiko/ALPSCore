# 
# alps-utilities cmake configuration script.
# 

cmake_minimum_required (VERSION 2.8)

# define project name
project (alps-utilities CXX)
message ("\nALPSCore module : ${PROJECT_NAME}\n")

# add common modules from ../common/cmake
list(APPEND CMAKE_MODULE_PATH ${PROJECT_SOURCE_DIR}/../common/cmake)
include(ALPSCommonModuleDefinitions)

if (NOT DocumentationOnly)
  include(ALPSEnableMPI)
  include(CheckIncludeFile)

  # this is uply, we should make this nicer
  option(ALPS_HAVE_PYTHON "Enable Python libraries" ON) 

  if(ALPS_HAVE_MPI)
    add_boost(filesystem system chrono program_options mpi)
  else()
    add_boost(filesystem system chrono program_options)
  endif()
  add_this_package()
  add_testing()
  CHECK_INCLUDE_FILE(unistd.h ALPS_HAVE_UNISTD_H)
  gen_hpp_config()
  gen_pkg_config()
  gen_find_module(alps/utilities/stacktrace.hpp)
endif (NOT DocumentationOnly)
gen_documentation()
