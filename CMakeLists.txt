cmake_minimum_required(VERSION 3.16)

project(caper
    VERSION 1.0
    LANGUAGES CXX
    DESCRIPTION "Caper parser generator")

set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")

# ---------------------------------------------------------------------------
# Boost: system install (BOOST_ROOT / Boost_ROOT) or NuGet under vcproj/packages
# ---------------------------------------------------------------------------
function(caper_setup_boost_from_nuget)
    set(_packages_root "${CMAKE_SOURCE_DIR}/caper/vcproj/packages")
    if(NOT EXISTS "${_packages_root}")
        return()
    endif()

    file(GLOB _boost_include_candidates
        "${_packages_root}/boost.*/lib/native/include")
    if(NOT _boost_include_candidates)
        return()
    endif()
    list(SORT _boost_include_candidates)
    list(GET _boost_include_candidates -1 _boost_include)

    if(CMAKE_SIZEOF_VOID_P EQUAL 8)
        set(_addr "address-model-64")
    else()
        set(_addr "address-model-32")
    endif()

    file(GLOB _fs_lib_dirs
        "${_packages_root}/boost_filesystem-*/lib/native/${_addr}/lib")
    file(GLOB _sys_lib_dirs
        "${_packages_root}/boost_system-*/lib/native/${_addr}/lib")
    if(NOT _fs_lib_dirs OR NOT _sys_lib_dirs)
        return()
    endif()
    list(SORT _fs_lib_dirs)
    list(SORT _sys_lib_dirs)
    list(GET _fs_lib_dirs -1 _fs_lib_dir)
    list(GET _sys_lib_dirs -1 _sys_lib_dir)

    # Prefer static, multithreaded, dynamic CRT libs (libboost_*-mt-*.lib without -s-).
    file(GLOB _fs_release "${_fs_lib_dir}/libboost_filesystem-*-mt-*.lib")
    file(GLOB _fs_debug   "${_fs_lib_dir}/libboost_filesystem-*-mt-gd-*.lib")
    file(GLOB _sys_release "${_sys_lib_dir}/libboost_system-*-mt-*.lib")
    file(GLOB _sys_debug   "${_sys_lib_dir}/libboost_system-*-mt-gd-*.lib")

    list(FILTER _fs_release EXCLUDE REGEX "-mt-s|-mt-gd|-mt-sgd")
    list(FILTER _sys_release EXCLUDE REGEX "-mt-s|-mt-gd|-mt-sgd")

    if(NOT _fs_release OR NOT _sys_release)
        return()
    endif()
    list(GET _fs_release 0 _fs_release_lib)
    list(GET _sys_release 0 _sys_release_lib)
    if(_fs_debug)
        list(GET _fs_debug 0 _fs_debug_lib)
    else()
        set(_fs_debug_lib "${_fs_release_lib}")
    endif()
    if(_sys_debug)
        list(GET _sys_debug 0 _sys_debug_lib)
    else()
        set(_sys_debug_lib "${_sys_release_lib}")
    endif()

    if(NOT TARGET Boost::boost)
        add_library(Boost::boost INTERFACE IMPORTED)
        target_include_directories(Boost::boost INTERFACE "${_boost_include}")
    endif()

    if(NOT TARGET Boost::system)
        add_library(Boost::system STATIC IMPORTED)
        set_target_properties(Boost::system PROPERTIES
            IMPORTED_LOCATION_RELEASE "${_sys_release_lib}"
            IMPORTED_LOCATION_DEBUG "${_sys_debug_lib}"
            IMPORTED_LOCATION "${_sys_release_lib}"
            INTERFACE_INCLUDE_DIRECTORIES "${_boost_include}")
    endif()

    if(NOT TARGET Boost::filesystem)
        add_library(Boost::filesystem STATIC IMPORTED)
        set_target_properties(Boost::filesystem PROPERTIES
            IMPORTED_LOCATION_RELEASE "${_fs_release_lib}"
            IMPORTED_LOCATION_DEBUG "${_fs_debug_lib}"
            IMPORTED_LOCATION "${_fs_release_lib}"
            INTERFACE_INCLUDE_DIRECTORIES "${_boost_include}"
            INTERFACE_LINK_LIBRARIES Boost::system)
    endif()

    set(Boost_FOUND TRUE PARENT_SCOPE)
    message(STATUS "Boost (NuGet): ${_boost_include}")
endfunction()

set(Boost_FOUND FALSE)
# CMake 3.30+/4.x removed FindBoost; prefer BoostConfig, then NuGet.
find_package(Boost CONFIG QUIET COMPONENTS filesystem)
if(NOT Boost_FOUND)
    caper_setup_boost_from_nuget()
endif()
if(NOT Boost_FOUND)
    message(FATAL_ERROR
        "Boost not found.\n"
        "  - Install Boost and set Boost_ROOT / CMAKE_PREFIX_PATH, or\n"
        "  - Restore NuGet packages under caper/vcproj "
        "(boost + boost_filesystem + boost_system).")
endif()

add_subdirectory(caper)
add_subdirectory(capella)

# ---------------------------------------------------------------------------
# publish: package built caper binary as a dated zip under ${CMAKE_BINARY_DIR}/tmp
# ---------------------------------------------------------------------------
string(TIMESTAMP CAPER_TODAY "%Y-%m-%d")
set(CAPER_PUBLISH_DIR "${CMAKE_BINARY_DIR}/tmp")
set(CAPER_PUBLISH_ZIP "${CAPER_PUBLISH_DIR}/caper-${CAPER_TODAY}.zip")

add_custom_target(publish
    COMMAND ${CMAKE_COMMAND} -E make_directory "${CAPER_PUBLISH_DIR}"
    COMMAND ${CMAKE_COMMAND} -E tar cf "${CAPER_PUBLISH_ZIP}" --format=zip
            "$<TARGET_FILE_NAME:caper>"
    COMMAND ${CMAKE_COMMAND} -E echo "Published ${CAPER_PUBLISH_ZIP}"
    WORKING_DIRECTORY "$<TARGET_FILE_DIR:caper>"
    DEPENDS caper
    COMMENT "Packaging caper binary"
    VERBATIM)
