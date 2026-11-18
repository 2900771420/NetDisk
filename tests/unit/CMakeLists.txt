# ===== DAY 33A MODIFIED BEGIN 使用功能目录并保持全部CTest名称不变 =====
function(add_netdisk_unit_test target source)
    add_executable(${target} ${source})
    target_link_libraries(${target} PRIVATE ${ARGN})
    add_test(NAME ${target} COMMAND ${target})
endfunction()

add_netdisk_unit_test(io_tests io/posix_io_tests.cpp
    netdisk_io)
add_netdisk_unit_test(request_line_tests http/request_line_tests.cpp
    netdisk_http)
add_netdisk_unit_test(request_headers_tests http/request_headers_tests.cpp
    netdisk_http)
add_netdisk_unit_test(request_body_tests http/request_parser_tests.cpp
    netdisk_http)
add_netdisk_unit_test(http_response_tests http/response_tests.cpp
    netdisk_server_lib)
add_netdisk_unit_test(stream_upload_tests storage/temp_upload_tests.cpp
    netdisk_server_lib)
add_netdisk_unit_test(publish_tests storage/publication_tests.cpp
    netdisk_server_lib)
add_netdisk_unit_test(object_path_tests storage/object_path_tests.cpp
    netdisk_storage)
add_netdisk_unit_test(download_headers_tests http/download_response_tests.cpp
    netdisk_http)
add_netdisk_unit_test(file_catalog_tests server/file_catalog_tests.cpp
    netdisk_server_lib)
add_netdisk_unit_test(file_mutation_tests server/file_mutation_tests.cpp
    netdisk_server_lib)
add_netdisk_unit_test(sqlite_lifecycle_tests metadata/sqlite_lifecycle_tests.cpp
    netdisk_metadata)
add_netdisk_unit_test(file_pagination_tests server/file_pagination_tests.cpp
    netdisk_server_lib)
add_netdisk_unit_test(publish_state_tests server/publish_visibility_tests.cpp
    netdisk_server_lib)
add_netdisk_unit_test(publishing_recovery_tests maintenance/publishing_recovery_tests.cpp
    netdisk_maintenance)
add_netdisk_unit_test(deletion_recovery_tests maintenance/deletion_recovery_tests.cpp
    netdisk_maintenance)
add_netdisk_unit_test(storage_audit_tests maintenance/storage_audit_tests.cpp
    netdisk_maintenance)
add_netdisk_unit_test(sha256_tests crypto/sha256_tests.cpp
    netdisk_crypto netdisk_metadata)
add_netdisk_unit_test(digest_backfill_tests maintenance/digest_backfill_tests.cpp
    netdisk_maintenance netdisk_http)
add_netdisk_unit_test(range_tests http/range_tests.cpp
    netdisk_http)
add_netdisk_unit_test(if_range_tests http/if_range_tests.cpp
    netdisk_http)
add_netdisk_unit_test(resume_download_tests client/resume_download_tests.cpp
    netdisk_client_lib)
# ===== DAY 33A MODIFIED END =====
