cmake_minimum_required(VERSION 3.20)

project(tcpstreaming C)

# Search for SQLite3 library for database
find_package(SQLite3 REQUIRED)

# Add cJSON library
add_subdirectory(vendor/cJSON)

# Create executables for server and client
add_executable(server server.c)
add_executable(client client.c)

# Link sqlite to executables
target_link_libraries(server sqlite3)
target_link_libraries(client sqlite3)

# Link cJSON to executables
target_link_libraries(server cjson)
target_link_libraries(client cjson)