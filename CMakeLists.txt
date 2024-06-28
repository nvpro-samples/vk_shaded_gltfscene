cmake_minimum_required(VERSION 3.17)

# get_filename_component(PROJNAME ${CMAKE_CURRENT_SOURCE_DIR} NAME)
set(PROJNAME vk_gltf_renderer)
Project(${PROJNAME})
Message(STATUS "-------------------------------")
Message(STATUS "Processing Project ${PROJNAME}:")

# Include CPM.cmake
include(cmake/CPM.cmake)

# Add RapidJSON package
CPMAddPackage(
    NAME RapidJSON
    GITHUB_REPOSITORY Tencent/rapidjson
    VERSION 1.1.0
    GIT_TAG v1.1.0
    DOWNLOAD_ONLY YES
)

set(RAPIDJSON_INCLUDE_DIR ${RapidJSON_SOURCE_DIR}/include/rapidjson)

#####################################################################################
# look for nvpro_core 1) as a sub-folder 2) at some other locations
# this cannot be put anywhere else since we still didn't find setup.cmake yet

if(NOT BASE_DIRECTORY)

  find_path(BASE_DIRECTORY
    NAMES nvpro_core/cmake/setup.cmake
    PATHS ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/.. ${CMAKE_CURRENT_SOURCE_DIR}/../.. ${CMAKE_CURRENT_SOURCE_DIR}/external
    REQUIRED
    DOC "Directory containing nvpro_core"
    )
endif()
if(EXISTS ${BASE_DIRECTORY}/nvpro_core/cmake/setup.cmake)
  set(OUTPUT_PATH ${CMAKE_CURRENT_BINARY_DIR}/bin_x64)
  include(${BASE_DIRECTORY}/nvpro_core/cmake/setup.cmake)
else()
  message(FATAL_ERROR "could not find base directory, please set BASE_DIRECTORY to folder containing nvpro_core")
endif()

_add_project_definitions(${PROJNAME})

set(NVPRO_CORE_DIR ${BASE_DIRECTORY}/nvpro_core)


#####################################################################################
# additions from packages needed for this sample
# add refs  in LIBRARIES_OPTIMIZED
# add refs  in LIBRARIES_DEBUG
# add files in PACKAGE_SOURCE_FILES

_add_package_VulkanSDK()
_add_package_ShaderC()
_add_package_IMGUI()
_add_package_NVML()

#####################################################################################
# process the rest of some cmake code that needs to be done *after* the packages add
_add_nvpro_core_lib()

#####################################################################################
# Source files for this project
#
file(GLOB SOURCE_FILES src/*.*)
file(GLOB SHADER_SOURCE shaders/*.glsl)
file(GLOB SHADER_HEADER shaders/*.h)
file(GLOB SHADER_FILES shaders/*.glsl shaders/*.h)
file(GLOB SHADER_COMMON_FILES ${NVPRO_CORE_DIR}/nvvkhl/shaders/*.*)

include_directories(${CMAKE_CURRENT_SOURCE_DIR})

#####################################################################################
# Executable
#

add_executable(${PROJNAME} ${SOURCE_FILES} ${COMMON_SOURCE_FILES} ${PACKAGE_SOURCE_FILES} ${SHADER_FILES} ${SHADER_COMMON_FILES})

set_property(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} PROPERTY VS_STARTUP_PROJECT ${PROJNAME})

target_compile_definitions(${PROJNAME} 
    PRIVATE IMGUI_DEFINE_MATH_OPERATORS )

target_compile_definitions(${PROJNAME} 
    PRIVATE NVPRO_CORE_DIR="${NVPRO_CORE_DIR}" )

# Adding warning levels for MSVC
if(MSVC)
    target_compile_options(${PROJNAME}  PRIVATE /W3)
endif()

# Include the rapidjson headers
target_include_directories(${PROJNAME} PRIVATE ${RAPIDJSON_INCLUDE_DIR})


#####################################################################################
# common source code needed for this sample
#
source_group(common FILES 
  ${COMMON_SOURCE_FILES}
  ${PACKAGE_SOURCE_FILES}
)
source_group("Shader Files" FILES ${SHADER_FILES})
source_group("Shader Files/common" FILES ${SHADER_COMMON_FILES})
# source_group("Source Files" FILES ${SOURCE_FILES})


if(UNIX)
  set(UNIXLINKLIBS dl pthread)
else()
  set(UNIXLINKLIBS)
endif()

#####################################################################################
# Linkage
#

target_link_libraries(${PROJNAME} ${PLATFORM_LIBRARIES} nvpro_core)

foreach(DEBUGLIB ${LIBRARIES_DEBUG})
  target_link_libraries(${PROJNAME} debug ${DEBUGLIB})
endforeach(DEBUGLIB)

foreach(RELEASELIB ${LIBRARIES_OPTIMIZED})
  target_link_libraries(${PROJNAME} optimized ${RELEASELIB})
endforeach(RELEASELIB)

# Resources
download_files(FILENAMES FlightHelmet.zip EXTRACT)
download_files(FILENAMES daytime.hdr std_env.hdr)


#####################################################################################
# GLSL Shader compilation
# - Shaders are compiled to Spir-V but not embedded in the binary
# - This allow to check for errors before running the application
#
 # GLSL compilation flags
 set(_GLSL_FLAGS 
   -Ishaders 
   -I${NVPRO_CORE_DIR} 
   -g # Debugging information
   -D_glsl)
 
 # Compiling shaders to Spir-V header
 compile_glsl(
     SOURCE_FILES ${SHADER_SOURCE}
     HEADER_FILES ${SHADER_HEADER}
     DST "${CMAKE_CURRENT_SOURCE_DIR}/_autogen" 
     VULKAN_TARGET "vulkan1.3"
     # HEADER ON
     DEPENDENCY OFF
     FLAGS ${_GLSL_FLAGS}
     )

 target_sources(${PROJNAME} PRIVATE ${GLSL_SOURCES} ${GLSL_HEADERS})

#####################################################################################
# copies binaries that need to be put next to the exe files (ZLib, etc.)
#
_finalize_target( ${PROJNAME} )

install(FILES ${SHADER_FILES} CONFIGURATIONS Release DESTINATION "bin_${ARCH}/GLSL_${PROJNAME}")
install(FILES ${SHADER_FILES} CONFIGURATIONS Debug DESTINATION "bin_${ARCH}_debug/GLSL_${PROJNAME}")
install(FILES ${SHADER_COMMON_FILES} CONFIGURATIONS Release DESTINATION "bin_${ARCH}/GLSL_${PROJNAME}/nvvkhl/shaders")
install(FILES ${SHADER_COMMON_FILES} CONFIGURATIONS Debug DESTINATION "bin_${ARCH}_debug/GLSL_${PROJNAME}/nvvkhl/shaders")
