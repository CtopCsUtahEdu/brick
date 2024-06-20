cmake_minimum_required(VERSION 3.13)
project(brick
        VERSION 0.1
        LANGUAGES CXX)

cmake_policy(SET CMP0077 NEW)

set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_EXTENSIONS OFF)
set(IgnoreMe "${CMAKE_C_COMPILER}")

include(CheckLanguage)
include(CheckCXXCompilerFlag)
include(CMakePackageConfigHelpers)
include(GNUInstallDirs)
check_language(CUDA)

if (CMAKE_CUDA_COMPILER)
    enable_language(CUDA)
    set(CMAKE_CUDA_STANDARD ${CMAKE_CXX_STANDARD})
    set(CMAKE_CUDA_SEPARABLE_COMPILATION OFF)
endif ()

option(BRICK_USE_HIP "Use HIP targets" OFF)
# hip/samples/2_Cookbook/12_cmake_hip_add_executable
if (BRICK_USE_HIP)
    cmake_minimum_required(VERSION 3.21)
    if(DEFINED ENV{HIP_PATH})
        set(HIP_PATH $ENV{HIP_PATH})
    else()
        set(HIP_PATH /opt/rocm/hip)
    endif()
    set(CMAKE_MODULE_PATH "${HIP_PATH}/cmake" ${CMAKE_MODULE_PATH})
    list(APPEND CMAKE_PREFIX_PATH "${HIP_PATH}" "${HIP_PATH}/..")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -D__HIP_PLATFORM_AMD__")
    find_package(HIP MODULE)
    if (NOT HIP_FOUND)
        message(FATAL_ERROR "HIP not found with USE_HIP specified")
    endif()
    find_package(hip REQUIRED)
    set(CMAKE_HIP_ARCHITECTURES gfx908 gfx90a)
endif()

find_package(OpenMP REQUIRED)
find_package(MPI)
if (CMAKE_CUDA_COMPILER)
  find_package(OpenCL 2.0)
endif ()

option(BRICK_USE_MEMFD "Using memfd instead of shm_open, supported on Linux >= 3.17 with \"CONFIG_MEMFD_CREATE\"" OFF)
if (BRICK_USE_MEMFD)
    add_compile_definitions(USE_MEMFD)
endif ()

option(BRICK_CUDA_AWARE "Use CUDA_AWARE communication" OFF)
if (BRICK_CUDA_AWARE)
    add_compile_definitions(CUDA_AWARE)
endif ()

option(BRICK_USE_TYPES "Use MPI_TYPES" OFF)
if (BRICK_USE_TYPES)
    add_compile_definitions(USE_TYPES)
endif ()

option(BRICK_BARRIER_TIMESTEP "Use barrier to reduce timing variation" OFF)
if (BRICK_BARRIER_TIMESTEP)
    add_compile_definitions(BARRIER_TIMESTEP)
endif ()

option(BRICK_BUILD_TEST "Include various tests as build target" ON)

option(HAS_HMM "Use ATS/HMM for CUDA (not-used)" OFF)

# MacOS doesn't have librt
# shm_open is recommended to link with -lrt (man page)
find_library(LIBRT rt)
if (LIBRT)
    message(STATUS "LIBRT ${LIBRT}")
else ()
    message(STATUS "LIBRT not found, assuming its a Mac")
    set(LIBRT "")
endif ()

check_cxx_compiler_flag(-fsycl HAS_SYCL)

if (HAS_SYCL)
    # Sycl requires 17
    set(CMAKE_CXX_STANDARD 17)
    get_filename_component(bin_dir "${CMAKE_CXX_COMPILER}" PATH)
    get_filename_component(SYCL_DIR "${bin_dir}" PATH)

    message(STATUS "Setting include/lib path according to compiler path: ${CMAKE_CXX_COMPILER}")
    message(STATUS "SYCL compiler install dir: ${SYCL_DIR}")
    set(CMAKE_BUILD_RPATH "${SYCL_DIR}/lib")
endif ()

# BRICK_PROJECT_PATH is set when brick is used as a submodule
# parent project should take care of VecScatter initialization
if (NOT BRICK_PROJECT_PATH)
    set(BRICK_PROJECT_PATH "${CMAKE_CURRENT_SOURCE_DIR}")
    include(cmake/VecScatter.cmake)
endif ()

# Avoid using O3
set(CMAKE_CXX_FLAGS_RELEASE "-O2 -DNDEBUG")

set(BRICK_BUILD_ARCH "native" CACHE STRING "Specify build architecture target")

check_cxx_compiler_flag("-march=${BRICK_BUILD_ARCH}" HAS_MARCH)
if (HAS_MARCH)
    message(STATUS "Using march=${BRICK_BUILD_ARCH}")
    add_compile_options(-march=${BRICK_BUILD_ARCH})
else ()
    # Work around Summit only have mcpu flag
    check_cxx_compiler_flag("-mcpu=${BRICK_BUILD_ARCH}" HAS_MCPU)
    if (HAS_MCPU)
        message(STATUS "Using mcpu=${BRICK_BUILD_ARCH}")
        add_compile_options(-mcpu=${BRICK_BUILD_ARCH})
    endif ()
endif ()

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${OpenMP_CXX_FLAGS}")
set(CMAKE_CUDA_FLAGS "${CMAKE_CUDA_FLAGS} -Xcompiler \"${CMAKE_CXX_FLAGS}\"")
set(CMAKE_HIP_FLAGS "${OpenMP_CXX_FLAGS}")

include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}
)

if (MPI_FOUND)
    include_directories(
        ${MPI_CXX_HEADER_DIR}
    )
endif()

add_subdirectory(src)
if (BRICK_BUILD_TEST)
    add_subdirectory(single)
    if (MPI_FOUND)
        add_subdirectory(weak)
        add_subdirectory(strong)
    endif()
endif ()

# Generating html doc from doxygen output
set(PROJECT_INC_DIRS "${PROJECT_SOURCE_DIR}/include")

find_package(Doxygen
        COMPONENTS dot)

if (DOXYGEN_FOUND)
    configure_file(docs/Doxyfile.in ${CMAKE_CURRENT_BINARY_DIR}/Doxyfile @ONLY)
    add_custom_target(doxydoc
            Doxygen::doxygen ${CMAKE_CURRENT_BINARY_DIR}/Doxyfile
            WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}
            COMMENT "Generating API documentation with Doxygen" VERBATIM
            )
endif ()

# Install script

# Install headers
install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/${CMAKE_PROJECT_NAME}
        FILES_MATCHING PATTERN "*.h")

# Install codegen
install(DIRECTORY codegen/ DESTINATION ${CMAKE_INSTALL_LIBDIR}/${CMAKE_PROJECT_NAME}/codegen
        PATTERN "__pycache__" EXCLUDE)

# Install codegen config
install(DIRECTORY cmake/ DESTINATION ${CMAKE_INSTALL_LIBDIR}/${CMAKE_PROJECT_NAME}/cmake
        FILES_MATCHING PATTERN "*.cmake")

configure_package_config_file(
    "${PROJECT_SOURCE_DIR}/brickConfig.cmake.in"
    "${PROJECT_BINARY_DIR}/brickConfig.cmake"
    INSTALL_DESTINATION "${CMAKE_INSTALL_LIBDIR}/${PROJECT_NAME}/cmake")

write_basic_package_version_file(
    "${PROJECT_BINARY_DIR}/brickConfigVersion.cmake"
    COMPATIBILITY AnyNewerVersion)

install(EXPORT brick-targets 
    DESTINATION "${CMAKE_INSTALL_LIBDIR}/${PROJECT_NAME}/cmake")

install(FILES "${CMAKE_CURRENT_BINARY_DIR}/brickConfig.cmake"
              "${CMAKE_CURRENT_BINARY_DIR}/brickConfigVersion.cmake"
        DESTINATION "${CMAKE_INSTALL_LIBDIR}/${PROJECT_NAME}/cmake")
