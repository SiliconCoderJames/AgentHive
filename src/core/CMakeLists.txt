add_library(zplatform_core STATIC
    util.cpp
    db/database.cpp
    embed/embedder.cpp
    services/agent_service.cpp
    services/knowledge_service.cpp
    services/skill_service.cpp
    services/memory_service.cpp
    services/message_service.cpp
    services/error_service.cpp
    services/usage_service.cpp
    services/audit_service.cpp
    platform.cpp
    http/server.cpp
)
target_include_directories(zplatform_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)
# 与 sqlite3_static / sqlite_vec_static 一致：静态编译进进程，不走扩展加载机制
target_compile_definitions(zplatform_core PUBLIC SQLITE_CORE)
target_link_libraries(zplatform_core PUBLIC
    sqlite3_static
    sqlite_vec_static
    nlohmann_json::nlohmann_json
    httplib_hdr
)

# 把 schema.sql 嵌入二进制
file(READ ${CMAKE_CURRENT_SOURCE_DIR}/db/schema.sql SCHEMA_SQL_CONTENT)
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/db/schema_sql.h.in
               ${CMAKE_CURRENT_BINARY_DIR}/generated/schema_sql.h @ONLY)
target_include_directories(zplatform_core PUBLIC ${CMAKE_CURRENT_BINARY_DIR}/generated)
