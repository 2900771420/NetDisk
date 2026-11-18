# ===== ARCHITECTURE GUARD ADDED BEGIN 固化第33A模块边界 =====
function(_netdisk_assert_same_list description actual_values expected_values)
    set(actual ${actual_values})
    set(expected ${expected_values})
    list(REMOVE_DUPLICATES actual)
    list(REMOVE_DUPLICATES expected)
    list(SORT actual)
    list(SORT expected)

    if(NOT "${actual}" STREQUAL "${expected}")
        message(FATAL_ERROR
            "NetDisk architecture violation: ${description}\n"
            "  actual: ${actual}\n"
            "  expected: ${expected}"
        )
    endif()
endfunction()

function(_netdisk_assert_target_type target_name expected_type)
    if(NOT TARGET "${target_name}")
        message(FATAL_ERROR
            "NetDisk architecture violation: missing target ${target_name}"
        )
    endif()

    get_target_property(actual_type "${target_name}" TYPE)
    if(NOT "${actual_type}" STREQUAL "${expected_type}")
        message(FATAL_ERROR
            "NetDisk architecture violation: ${target_name} must be "
            "${expected_type} but is ${actual_type}"
        )
    endif()
endfunction()

function(_netdisk_assert_direct_dependencies target_name)
    set(expected_dependencies ${ARGN})
    get_target_property(actual_dependencies "${target_name}" LINK_LIBRARIES)
    if("${actual_dependencies}" MATCHES "-NOTFOUND$")
        set(actual_dependencies)
    endif()

    _netdisk_assert_same_list(
        "direct dependencies of ${target_name}"
        "${actual_dependencies}"
        "${expected_dependencies}"
    )
endfunction()

function(_netdisk_assert_client_closure)
    set(allowed_internal_targets
        netdisk_client_lib
        netdisk_api
        netdisk_io
        netdisk_crypto
        netdisk_http
    )
    set(forbidden_dependencies
        netdisk_server_lib
        netdisk_storage
        netdisk_metadata
        netdisk_maintenance
        SQLite::SQLite3
        nlohmann_json::nlohmann_json
    )
    set(pending_targets netdisk_client_lib)
    set(visited_targets)

    while(pending_targets)
        list(POP_FRONT pending_targets current_target)
        if(current_target IN_LIST visited_targets)
            continue()
        endif()
        list(APPEND visited_targets "${current_target}")

        if(NOT current_target IN_LIST allowed_internal_targets)
            message(FATAL_ERROR
                "NetDisk architecture violation: client link closure contains "
                "internal target ${current_target}"
            )
        endif()

        get_target_property(interface_dependencies
            "${current_target}" INTERFACE_LINK_LIBRARIES)
        if("${interface_dependencies}" MATCHES "-NOTFOUND$")
            set(interface_dependencies)
        endif()

        foreach(dependency IN LISTS interface_dependencies)
            foreach(forbidden IN LISTS forbidden_dependencies)
                string(FIND "${dependency}" "${forbidden}" forbidden_position)
                if(forbidden_position GREATER_EQUAL 0)
                    message(FATAL_ERROR
                        "NetDisk architecture violation: client link closure "
                        "reaches ${forbidden} through ${current_target}"
                    )
                endif()
            endforeach()

            if(TARGET "${dependency}" AND "${dependency}" MATCHES "^netdisk_")
                list(APPEND pending_targets "${dependency}")
            endif()
        endforeach()
    endwhile()
endfunction()

function(netdisk_verify_architecture)
    set(expected_modules
        api
        client
        crypto
        http
        io
        maintenance
        metadata
        server
        storage
    )

    file(GLOB source_children
        LIST_DIRECTORIES true
        RELATIVE "${PROJECT_SOURCE_DIR}/src"
        "${PROJECT_SOURCE_DIR}/src/*"
    )
    set(actual_modules)
    foreach(child IN LISTS source_children)
        if(IS_DIRECTORY "${PROJECT_SOURCE_DIR}/src/${child}")
            list(APPEND actual_modules "${child}")
        endif()
    endforeach()
    _netdisk_assert_same_list(
        "production module directories"
        "${actual_modules}"
        "${expected_modules}"
    )

    set(source_directories
        "${PROJECT_SOURCE_DIR}"
        "${PROJECT_SOURCE_DIR}/src"
    )
    foreach(module IN LISTS expected_modules)
        list(APPEND source_directories
            "${PROJECT_SOURCE_DIR}/src/${module}")
    endforeach()

    foreach(source_directory IN LISTS source_directories)
        get_property(directory_includes
            DIRECTORY "${source_directory}" PROPERTY INCLUDE_DIRECTORIES)
        if(directory_includes)
            message(FATAL_ERROR
                "NetDisk architecture violation: directory-scoped include "
                "paths are forbidden in ${source_directory}: "
                "${directory_includes}"
            )
        endif()
    endforeach()

    set(production_directories "${PROJECT_SOURCE_DIR}/src")
    foreach(module IN LISTS expected_modules)
        list(APPEND production_directories
            "${PROJECT_SOURCE_DIR}/src/${module}")
    endforeach()

    set(actual_targets)
    foreach(source_directory IN LISTS production_directories)
        get_property(directory_targets
            DIRECTORY "${source_directory}" PROPERTY BUILDSYSTEM_TARGETS)
        list(APPEND actual_targets ${directory_targets})
    endforeach()

    set(expected_targets
        netdisk
        netdisk_api
        netdisk_client_lib
        netdisk_crypto
        netdisk_download
        netdisk_http
        netdisk_io
        netdisk_maintenance
        netdisk_metadata
        netdisk_server_lib
        netdisk_storage
    )
    _netdisk_assert_same_list(
        "production CMake targets"
        "${actual_targets}"
        "${expected_targets}"
    )

    foreach(library_target IN ITEMS
            netdisk_api
            netdisk_client_lib
            netdisk_crypto
            netdisk_http
            netdisk_io
            netdisk_maintenance
            netdisk_metadata
            netdisk_server_lib
            netdisk_storage)
        _netdisk_assert_target_type("${library_target}" STATIC_LIBRARY)
    endforeach()
    _netdisk_assert_target_type(netdisk EXECUTABLE)
    _netdisk_assert_target_type(netdisk_download EXECUTABLE)

    _netdisk_assert_direct_dependencies(netdisk_api)
    _netdisk_assert_direct_dependencies(netdisk_io)
    _netdisk_assert_direct_dependencies(netdisk_crypto
        OpenSSL::Crypto)
    _netdisk_assert_direct_dependencies(netdisk_http
        netdisk_api netdisk_crypto)
    _netdisk_assert_direct_dependencies(netdisk_metadata
        netdisk_api netdisk_crypto SQLite::SQLite3)
    _netdisk_assert_direct_dependencies(netdisk_storage
        netdisk_api netdisk_io netdisk_crypto)
    _netdisk_assert_direct_dependencies(netdisk_maintenance
        netdisk_io netdisk_crypto netdisk_metadata netdisk_storage)
    _netdisk_assert_direct_dependencies(netdisk_server_lib
        netdisk_io netdisk_http netdisk_metadata netdisk_storage
        nlohmann_json::nlohmann_json)
    _netdisk_assert_direct_dependencies(netdisk_client_lib
        netdisk_api netdisk_io netdisk_crypto netdisk_http)
    _netdisk_assert_direct_dependencies(netdisk
        netdisk_server_lib netdisk_maintenance)
    _netdisk_assert_direct_dependencies(netdisk_download
        netdisk_client_lib)

    file(GLOB_RECURSE production_headers
        CONFIGURE_DEPENDS
        LIST_DIRECTORIES false
        "${PROJECT_SOURCE_DIR}/src/*.h"
        "${PROJECT_SOURCE_DIR}/src/*.hpp"
    )
    foreach(header IN LISTS production_headers)
        file(RELATIVE_PATH relative_header
            "${PROJECT_SOURCE_DIR}/src" "${header}")
        string(REPLACE "\\" "/" relative_header "${relative_header}")
        string(REPLACE "/" ";" header_parts "${relative_header}")
        list(LENGTH header_parts header_part_count)
        if(header_part_count LESS 5)
            message(FATAL_ERROR
                "NetDisk architecture violation: public header has an "
                "unstable path ${relative_header}"
            )
        endif()

        list(GET header_parts 0 header_module)
        list(GET header_parts 1 include_directory)
        list(GET header_parts 2 namespace_directory)
        list(GET header_parts 3 namespace_module)
        if(NOT header_module IN_LIST expected_modules OR
           NOT "${include_directory}" STREQUAL "include" OR
           NOT "${namespace_directory}" STREQUAL "netdisk" OR
           NOT "${namespace_module}" STREQUAL "${header_module}")
            message(FATAL_ERROR
                "NetDisk architecture violation: public header must be under "
                "src/<module>/include/netdisk/<module>: ${relative_header}"
            )
        endif()
    endforeach()

    file(GLOB_RECURSE production_sources
        CONFIGURE_DEPENDS
        LIST_DIRECTORIES false
        "${PROJECT_SOURCE_DIR}/src/*.cpp"
        "${PROJECT_SOURCE_DIR}/src/*.h"
        "${PROJECT_SOURCE_DIR}/src/*.hpp"
    )
    foreach(source_file IN LISTS production_sources)
        file(RELATIVE_PATH relative_source
            "${PROJECT_SOURCE_DIR}/src" "${source_file}")
        string(REPLACE "\\" "/" relative_source "${relative_source}")
        string(TOLOWER "${relative_source}" lowercase_source)
        if(lowercase_source MATCHES "(^|/|_)(day|lesson)[0-9]+")
            message(FATAL_ERROR
                "NetDisk architecture violation: production filename must not "
                "follow a teaching unit: ${relative_source}"
            )
        endif()
    endforeach()

    _netdisk_assert_client_closure()

    message(STATUS
        "NetDisk architecture guard passed: 9 modules, 11 production targets, "
        "clean client closure"
    )
endfunction()
# ===== ARCHITECTURE GUARD ADDED END =====
