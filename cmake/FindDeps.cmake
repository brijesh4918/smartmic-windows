# Third-party dependencies, all discovered via pkg-config so the build works
# the same on a developer machine and in CI without vendored binaries.
find_package(PkgConfig REQUIRED)

pkg_check_modules(SODIUM REQUIRED IMPORTED_TARGET libsodium)
pkg_check_modules(OPUS REQUIRED IMPORTED_TARGET opus)

find_path(NLOHMANN_JSON_INCLUDE_DIR nlohmann/json.hpp
          PATHS /opt/homebrew/include /usr/local/include /usr/include)
if(NOT NLOHMANN_JSON_INCLUDE_DIR)
    message(FATAL_ERROR "nlohmann/json.hpp not found (brew install nlohmann-json)")
endif()
