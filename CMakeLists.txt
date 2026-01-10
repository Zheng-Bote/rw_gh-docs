cmake_minimum_required(VERSION 3.23)

# cmake -S . -B build
# cmake --build build -j$(nproc)

project(gh_docs_bot LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

include(FetchContent)

# nlohmann/json via FetchContent
FetchContent_Declare(
  nlohmann_json
  GIT_REPOSITORY https://github.com/nlohmann/json.git
  GIT_TAG v3.11.2 # optional: wähle eine passende Version
)
FetchContent_MakeAvailable(nlohmann_json)

# Optional: Du kannst hier weitere FetchContent-Dependencies hinzufügen

# libcurl via find_package (System-abhängig)
find_package(CURL REQUIRED)

add_executable(gh_docs_bot
    src/main.cpp
)

target_include_directories(gh_docs_bot PRIVATE ${CURL_INCLUDE_DIRS})
target_link_libraries(gh_docs_bot PRIVATE CURL::libcurl nlohmann_json::nlohmann_json)

# Install rule (optional)
install(TARGETS gh_docs_bot RUNTIME DESTINATION bin)
