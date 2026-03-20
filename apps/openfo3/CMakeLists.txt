set(OPENFO3_SOURCES
    engine.cpp
    main.cpp
    options.cpp
)

set(OPENFO3_HEADERS
    engine.hpp
    options.hpp
)

source_group(apps/openfo3 FILES ${OPENFO3_SOURCES} ${OPENFO3_HEADERS})

openmw_add_executable(openfo3
    ${OPENFO3_SOURCES}
    ${OPENFO3_HEADERS}
)

target_link_libraries(openfo3
    components
)

if (WIN32)
    target_sources(openfo3 PRIVATE ${CMAKE_SOURCE_DIR}/files/windows/other-apps.manifest)
endif()

if (BUILD_WITH_CODE_COVERAGE)
    target_compile_options(openfo3 PRIVATE --coverage)
    target_link_libraries(openfo3 gcov)
endif()

if (MSVC AND PRECOMPILE_HEADERS_WITH_MSVC)
    target_precompile_headers(openfo3 PRIVATE
        <array>
        <filesystem>
        <memory>
        <optional>
        <span>
        <string>
        <string_view>
        <unordered_map>
        <utility>
        <variant>
        <vector>
    )
endif()
