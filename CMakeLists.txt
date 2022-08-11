# Copyright 2022 Fabien Chaix
# SPDX-License-Identifier: LGPL-2.1-only

cmake_minimum_required(VERSION 3.13)  # CMake version check
cmake_policy(SET CMP0077 NEW)
project(taz) 
enable_language(C CXX)
set(CMAKE_CXX_STANDARD 17)            # Enable c++17 standard

include(CTest)


set(ENABLE_SCOTCH         OFF     CACHE BOOL      "Force disabling Scotch (e.g. if you do not have Fortrand/Bison/Flex installed)" )
set(ENABLE_FLATBUFFERS    OFF     CACHE BOOL      "Force disabling Flatbuffers (e.g. if you do not want to profile apps or use taz-native traces)" )
set(ADD_HEAVY_TESTS       OFF     CACHE BOOL      "Add compute-heavy (~hours) tests to the CTest environment")
set(PROFILE_MPI_FLAVOR    "ompi"  CACHE STRING    "Select MPI flavor to use.")
set(PROFILE_GENERATE_FBS  OFF     CACHE BOOL      "Generate flatbuffer (otherwise just reuse header already generated)")

#------------------------------------------------------------------------
#
# Set compiler configuration
#
#------------------------------------------------------------------------

include(CheckIPOSupported)
check_ipo_supported(RESULT supported OUTPUT error)
#
#if( supported )
#    message(STATUS "IPO / LTO enabled")
#else()
#    message(STATUS "IPO / LTO not supported: <${error}>")
#endif()

if (MSVC)
    message(STATUS "Using MSVC-specific configuration...")
    # warning level 4 and all warnings as errors
    set(CMAKE_INTERPROCEDURAL_OPTIMIZATION TRUE)
    add_compile_options("/MP")
    add_compile_options("/W4")
    add_compile_options("/wd5030") #/WX
    set(BOOST_ROOT "C:/local/boost_1_80_0")
    set(BOOST_LIBRARYDIR "C:\\local\\boost_1_80_0\\lib64-msvc-14.0\\")
    set(Boost_LIB_PREFIX "lib")
    set(Boost_USE_STATIC_LIBS ON)
    set(Boost_STACKTRACE_VARIANT "windbg_cached")
    set(PYTHON_COMMAND "python3" )
    set(MPIEXEC_EXECUTABLE "C:\\Program Files\\Microsoft MPI\\Bin\\mpiexec.exe" )
    set(MPI_HOME "C:\\Program Files (x86)\\Microsoft SDKs\\MPI")
    set(TEST_GENERATION_TYPE "msvc")
else()
    message(STATUS "Using GCC/LLVM-specific configuration...")
    # lots of warnings
    add_compile_options(-flto=auto -march=native -Wall -Wextra -pedantic )

    #Make very sure we use Dwarf 4 (and not dwarf 5, so Valgrind is happy)
    # -DSCOTCH_DEBUG_ALL -DSCOTCH_DEBUG_FULL
    set(CMAKE_CXX_FLAGS_DEBUG "-gdwarf-4" CACHE STRING "Flags used by the C++ compiler during debug builds." FORCE)
    set(CMAKE_C_FLAGS_DEBUG "-gdwarf-4" CACHE STRING "Flags used by the C compiler during debug builds." FORCE)
    set(CMAKE_EXE_LINKER_FLAGS_DEBUG "-gdwarf-4" CACHE STRING "Flags used for linking binaries during debug builds." FORCE)
    set(CMAKE_SHARED_LINKER_FLAGS_DEBUG "-gdwarf-4" CACHE STRING "Flags used by the shared libraries linker during debug builds." FORCE)


    set(CMAKE_CXX_FLAGS_DEBUG "-gdwarf-4")
    set(Boost_STACKTRACE_VARIANT "basic")
    if(NOT EXISTS "${CMAKE_SOURCE_DIR}/scripts/venv")
        execute_process(COMMAND python3 "-m" "venv" "${CMAKE_SOURCE_DIR}/scripts/venv")
    endif(NOT EXISTS "${CMAKE_SOURCE_DIR}/scripts/venv")
    set(PYTHON_COMMAND "${CMAKE_SOURCE_DIR}/scripts/venv/bin/python3" )
    set(TEST_GENERATION_TYPE "gnu")

    #Asan build type to check sanity 
    #Based on https://gitlab.kitware.com/cmake/community/-/wikis/FAQ#how-can-i-extend-the-build-modes-with-a-custom-made-one-
    set(CMAKE_CXX_FLAGS_ASAN "-gdwarf-4 -DSCOTCH_DETERMINISTIC -Wall -fno-omit-frame-pointer -fsanitize=address -fsanitize=undefined -fsanitize=leak" CACHE STRING
 "Flags used by the C++ compiler during asan builds."
  FORCE)
set(CMAKE_C_FLAGS_ASAN "-gdwarf-4 -DSCOTCH_DETERMINISTIC -Wall -pedantic -fno-omit-frame-pointer -fsanitize=address -fsanitize=undefined -fsanitize=leak" CACHE STRING
  "Flags used by the C compiler during asan builds."
  FORCE)
set(CMAKE_EXE_LINKER_FLAGS_ASAN
  "-gdwarf-4 -Wl,--warn-unresolved-symbols,--warn-once -fno-omit-frame-pointer -fsanitize=address -fsanitize=undefined -fsanitize=leak" CACHE STRING
  "Flags used for linking binaries during asan builds."
  FORCE)
set(CMAKE_SHARED_LINKER_FLAGS_ASAN
  "-gdwarf-4 -Wl,--warn-unresolved-symbols,--warn-once -fno-omit-frame-pointer -fsanitize=address -fsanitize=undefined -fsanitize=leak" CACHE STRING
  "Flags used by the shared libraries linker during asan builds."
  FORCE)
mark_as_advanced(
  CMAKE_CXX_FLAGS_ASAN
  CMAKE_C_FLAGS_ASAN
  CMAKE_EXE_LINKER_FLAGS_ASAN
  CMAKE_SHARED_LINKER_FLAGS_ASAN)
# Update the documentation string of CMAKE_BUILD_TYPE for GUIs
set(CMAKE_BUILD_TYPE "${CMAKE_BUILD_TYPE}" CACHE STRING
  "Choose the type of build, options are: None Debug Release RelWithDebInfo MinSizeRel Asan."
  FORCE)

    # To get checks built in: 
endif()

#------------------------------------------------------------------------
#
# Find common dependencies and detect configuration
#
#------------------------------------------------------------------------

#set(Boost_VERBOSE ON)
#Alternatives for stacktrace are : 
# * stacktrace_noop 
# * stacktrace_basic 
# * stacktrace_backtrace 
# * stacktrace_addr2line
# * stacktrace_windbg
# * stacktrace_windbg_cached
find_package(Boost 1.74.0  REQUIRED COMPONENTS program_options "stacktrace_${Boost_STACKTRACE_VARIANT}" )
include_directories(${Boost_INCLUDE_DIRS})

set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} "${CMAKE_SOURCE_DIR}/cmake/")
find_package(PAPI)
find_package(MPI)

if( ${PROFILE_GENERATE_FBS} AND NOT ${MPI_FOUND} )
    message(WARNING "Cannot generate Flatbuffer, falling back to using existing headers")
    set(PROFILE_GENERATE_FBS OFF)
endif( ${PROFILE_GENERATE_FBS} AND NOT ${MPI_FOUND} )

#Detect presence of the Scotch library submodule
set(SCOTCH_ENABLED OFF)
if( ENABLE_SCOTCH AND EXISTS "${CMAKE_SOURCE_DIR}/extern/scotch/CMakeLists.txt")
    message(VERBOSE "Scotch library is enabled.")
    set(SCOTCH_ENABLED 1)
    enable_language(Fortran)
    set(BUILD_PTSCOTCH OFF)
    set(CMAKE_MODULE_PATH "${CMAKE_MODULE_PATH};${CMAKE_SOURCE_DIR}/extern/scotch/cmake/Modules")
    add_subdirectory("extern/scotch")
else(ENABLE_SCOTCH AND EXISTS "${CMAKE_SOURCE_DIR}/extern/scotch/CMakeLists.txt")
    message(STATUS "Scotch library is disabled (CMAKE_SOURCE_DIR=${CMAKE_SOURCE_DIR} and ENABLE_SCOTCH=${ENABLE_SCOTCH}).")
endif(ENABLE_SCOTCH AND EXISTS "${CMAKE_SOURCE_DIR}/extern/scotch/CMakeLists.txt")

#Detect presence of the Flatbuffer submodule
set(FLATBUFFERS_ENABLED OFF)
if( ENABLE_FLATBUFFERS AND EXISTS "${CMAKE_SOURCE_DIR}/extern/flatbuffers/CMakeLists.txt")
	message(VERBOSE "Flatbuffers are enabled.")
	set(FLATBUFFERS_ENABLED 1)
    set(CMAKE_MODULE_PATH "${CMAKE_MODULE_PATH};${CMAKE_SOURCE_DIR}/extern/flatbuffers/cmake/Modules")
    add_subdirectory("extern/flatbuffers")
else( ENABLE_FLATBUFFERS AND EXISTS "${CMAKE_SOURCE_DIR}/extern/flatbuffers/CMakeLists.txt")
	message(STATUS "Flatbuffers are disabled (ENABLE_FLATBUFFERS=${ENABLE_FLATBUFFERS}).")
endif( ENABLE_FLATBUFFERS AND EXISTS "${CMAKE_SOURCE_DIR}/extern/flatbuffers/CMakeLists.txt")

#Get the Python environment ready
message(VERBOSE "Initializing Python environment...")
execute_process(COMMAND "${PYTHON_COMMAND}" "-m" "ensurepip" "--upgrade" )
execute_process(COMMAND "${PYTHON_COMMAND}" "-m" "pip" "install"  "--upgrade" "pip")
execute_process(COMMAND "${PYTHON_COMMAND}" "-m" "pip" "install"  "--upgrade" "numpy")
execute_process(COMMAND "${PYTHON_COMMAND}" "-m" "pip" "install"  "--upgrade" "matplotlib")
execute_process(COMMAND "${PYTHON_COMMAND}" "-m" "pip" "install"  "--upgrade" "flatbuffers")
execute_process(COMMAND "${PYTHON_COMMAND}" "-m" "pip" "install"  "--upgrade" "gravis")
execute_process(COMMAND "${PYTHON_COMMAND}" "-m" "pip" "install"  "--upgrade" "jupyter")
execute_process(COMMAND "${PYTHON_COMMAND}" "-m" "pip" "install"  "--upgrade" " python-language-server")

 

message(VERBOSE  "Generate configuration header...")
configure_file(${CMAKE_SOURCE_DIR}/src/common/config.hpp.in ${CMAKE_SOURCE_DIR}/src/common/config.hpp)


#------------------------------------------------------------------------
#
# Generate flatbuffer serialization code 
#
#------------------------------------------------------------------------

set(FBS_HEADERS_PATH "")

if( ${FLATBUFFERS_ENABLED} )

    message(VERBOSE "Generating Flatbuffer headers...")

    set(MPI_PRIMITIVES_LIST_FOLDER "${CMAKE_SOURCE_DIR}/src/common/mpi_flavors/${PROFILE_MPI_FLAVOR}/" 
                                  CACHE STRING "The file from which to extract the configuration for MPI profiler")  
    execute_process(COMMAND python3 "${CMAKE_SOURCE_DIR}/scripts/generate_mpi_primitives.py" ${MPI_PRIMITIVES_LIST_FOLDER} ${MPI_CXX_HEADER_DIR} )
   
    set(FLATC_SCHEMAS_PATH "${CMAKE_SOURCE_DIR}/src/common/flatbuffers" )     

    flatbuffers_generate_headers(
         TARGET  taz_flatbuffers_cpp
         SCHEMAS ${MPI_PRIMITIVES_LIST_FOLDER}/instances.fbs;${FLATC_SCHEMAS_PATH}/property.fbs;${FLATC_SCHEMAS_PATH}/main.fbs;${FLATC_SCHEMAS_PATH}/matrix.fbs
         INCLUDE ${MPI_PRIMITIVES_LIST_FOLDER}
         FLAGS   --cpp)

    flatbuffers_generate_headers(
        TARGET  taz_flatbuffers_python
        SCHEMAS ${MPI_PRIMITIVES_LIST_FOLDER}/instances.fbs;${FLATC_SCHEMAS_PATH}/property.fbs;${FLATC_SCHEMAS_PATH}/main.fbs;${FLATC_SCHEMAS_PATH}/matrix.fbs
        INCLUDE ${MPI_PRIMITIVES_LIST_FOLDER}
        FLAGS   --python )
   
    set(FBS_HEADERS_PATH "${CMAKE_CURRENT_BINARY_DIR}/taz_flatbuffers")
    message(VERBOSE "Flatbuffer will generate headers to folder ${FBS_HEADERS_PATH}_{cpp,python}")

    add_custom_target (update_fbs_headers 
    COMMAND ${CMAKE_COMMAND} -E  # Copy python sources there
        copy_directory  "${FBS_HEADERS_PATH}_python/taz" ${CMAKE_SOURCE_DIR}/scripts/fbs
        COMMAND ${CMAKE_COMMAND} -E  # Copy C++ sources there
        copy_if_different "${FBS_HEADERS_PATH}_cpp/*" ${MPI_PRIMITIVES_LIST_FOLDER}/
    DEPENDS GENERATE_taz_flatbuffers_cpp GENERATE_taz_flatbuffers_python
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})

     #target_link_libraries(taz-simulate
     #    PRIVATE  generated_profile_headers_target
     #)
     message(STATUS "Generated new Flatbuffer headers. Optionally use the 'update_fbs_headers' make target to push them to versioned files.")

endif( ${FLATBUFFERS_ENABLED} )

#------------------------------------------------------------------------
#
# Get common sources
#
#------------------------------------------------------------------------

FILE(GLOB_RECURSE TAZ_COMMON_SOURCE_FILES src/common/**.cpp)

#------------------------------------------------------------------------
#
# Configure taz-simulate executable
#
#------------------------------------------------------------------------

# Add main.cpp file of project root directory as source file
FILE(GLOB_RECURSE TAZ_SIMULATE_SOURCE_FILES src/simulate/**.cpp)

#message(STATUS "Found taz-simulate sources ${TAZ_SIMULATE_SOURCE_FILES}")

# Add executable target with source files listed in SOURCE_FILES variable
add_executable(taz-simulate ${TAZ_SIMULATE_SOURCE_FILES} ${TAZ_COMMON_SOURCE_FILES})
# Report all warnings as errors
target_compile_options(taz-simulate  PRIVATE -Werror)    

#set_target_properties(taz PROPERTIES
#	VS_DEBUGGER_WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
#	VS_DEBUGGER_COMMAND_ARGUMENTS "${CMAKE_SOURCE_DIR}/test/point2point/simple/trace.index")

set(TAZ_SIMULATE_LIBRARIES ${Boost_LIBRARIES} )
target_link_libraries(taz-simulate )
#target_link_libraries(taz "C:/local/boost_1_80_0/lib64-msvc-14.0/libboost_program_options-vc140-mt-gd-x64-1_80.lib")
#target_link_libraries(taz "C:/local/boost_1_80_0/lib64-msvc-14.0/libboost_program_options-vc140-mt-x64-1_80.lib")

if(${PAPI_FOUND})
    list(APPEND TAZ_SIMULATE_LIBRARIES ${PAPI_LIBRARIES})
endif(${PAPI_FOUND})

if( ${SCOTCH_ENABLED}  )
    target_include_directories(taz-simulate PUBLIC "${CMAKE_CURRENT_BINARY_DIR}/extern/scotch/src/include")
    list(APPEND TAZ_SIMULATE_LIBRARIES scotch scotcherrexit)
endif( ${SCOTCH_ENABLED}  )

if( ${FLATBUFFERS_ENABLED}  )
    add_dependencies(taz-simulate GENERATE_taz_flatbuffers_cpp)
    target_include_directories(taz-simulate PUBLIC "${CMAKE_SOURCE_DIR}/extern/flatbuffers/include")
    target_include_directories(taz-simulate PUBLIC "${FBS_HEADERS_PATH}_cpp")
    #add_dependencies(taz-simulate GENERATE_generated_profile_headers_target)
endif( ${FLATBUFFERS_ENABLED} )

target_link_libraries(taz-simulate PUBLIC ${TAZ_SIMULATE_LIBRARIES} )

#------------------------------------------------------------------------
#
# Configure taz-show-matrix executable
#
#------------------------------------------------------------------------

FILE(GLOB_RECURSE TAZ_SHOW_MATRIX_SOURCE_FILES src/show-matrix/**.cpp)

# Add executable target with source files listed in SOURCE_FILES variable
add_executable(taz-show-matrix ${TAZ_SHOW_MATRIX_SOURCE_FILES}  ${TAZ_COMMON_SOURCE_FILES})
# Report all warnings as errors
target_compile_options(taz-show-matrix  PRIVATE -Werror)    

if( ${FLATBUFFERS_ENABLED}  )
    add_dependencies(taz-show-matrix GENERATE_taz_flatbuffers_cpp)
    target_include_directories(taz-show-matrix PUBLIC "${CMAKE_SOURCE_DIR}/extern/flatbuffers/include")
    target_include_directories(taz-show-matrix PUBLIC "${FBS_HEADERS_PATH}_cpp")
    #add_dependencies(taz-simulate GENERATE_generated_profile_headers_target)
endif( ${FLATBUFFERS_ENABLED} )

#------------------------------------------------------------------------
#
# Configure taz-show-csv executable
#
#------------------------------------------------------------------------

FILE(GLOB_RECURSE TAZ_SHOW_CSV_SOURCE_FILES src/show-csv/**.cpp)

# Add executable target with source files listed in SOURCE_FILES variable
add_executable(taz-show-csv ${TAZ_SHOW_CSV_SOURCE_FILES}  ${TAZ_COMMON_SOURCE_FILES})
# Report all warnings as errors
target_compile_options(taz-show-csv  PRIVATE -Werror)    

if( ${FLATBUFFERS_ENABLED}  )
    add_dependencies(taz-show-csv GENERATE_taz_flatbuffers_cpp)
    target_include_directories(taz-show-csv PUBLIC "${CMAKE_SOURCE_DIR}/extern/flatbuffers/include")
    target_include_directories(taz-show-csv PUBLIC "${FBS_HEADERS_PATH}_cpp")
    #add_dependencies(taz-simulate GENERATE_generated_profile_headers_target)
endif( ${FLATBUFFERS_ENABLED} )


#------------------------------------------------------------------------
#
# Configure tazprofile library
#
#------------------------------------------------------------------------

if( ${MPI_FOUND}  AND  ( ${FLATBUFFERS_ENABLED} OR NOT ${PROFILE_GENERATE_FBS} ) )
    message(STATUS "Enabling taz profile library...")

    FILE(GLOB_RECURSE TAZ_PROFILE_SOURCE_FILES src/profile/**.cpp)
    add_library(tazprofile SHARED ${TAZ_PROFILE_SOURCE_FILES}  ${TAZ_COMMON_SOURCE_FILES})
    target_link_libraries(tazprofile MPI::MPI_CXX)
    target_compile_options(tazprofile  PRIVATE -Werror)    

    target_include_directories(tazprofile PUBLIC "${CMAKE_SOURCE_DIR}/extern/flatbuffers/include")

    if( ${PROFILE_GENERATE_FBS} )
        target_include_directories(tazprofile PUBLIC "${FBS_HEADERS_PATH}_cpp")
        add_dependencies(tazprofile GENERATE_taz_flatbuffers_cpp)
    else( ${PROFILE_GENERATE_FBS} )
        target_include_directories(tazprofile PUBLIC "${CMAKE_SOURCE_DIR}/src/common/mpi_flavors/${PROFILE_MPI_FLAVOR}")
    endif( ${PROFILE_GENERATE_FBS} )
    
else( ${MPI_FOUND}  AND ( ${FLATBUFFERS_ENABLED} OR NOT ${PROFILE_GENERATE_FBS} ) )
    message(STATUS "NOT Enabling taz profile library!")
endif( ${MPI_FOUND}  AND ( ${FLATBUFFERS_ENABLED} OR NOT ${PROFILE_GENERATE_FBS} ) )

#------------------------------------------------------------------------
#
# Configure taz-aggregate executable
#
#------------------------------------------------------------------------




#------------------------------------------------------------------------
#
# Generate tests and run profiles
#
#------------------------------------------------------------------------



message(VERBOSE "Generate collective and synthetic traces")
execute_process(COMMAND "${PYTHON_COMMAND}" "${CMAKE_SOURCE_DIR}/scripts/generate_collective_trace.py")
execute_process(COMMAND "${PYTHON_COMMAND}" "${CMAKE_SOURCE_DIR}/scripts/generate_synth_trace.py")


message(VERBOSE "Generate tests and debug profiles")
execute_process(COMMAND "${PYTHON_COMMAND}" "${CMAKE_SOURCE_DIR}/scripts/make_test_profiles.py" ${TEST_GENERATION_TYPE} ${ADD_HEAVY_TESTS})

message(VERBOSE "Add tests to Ctest environment")
add_subdirectory("tests")
