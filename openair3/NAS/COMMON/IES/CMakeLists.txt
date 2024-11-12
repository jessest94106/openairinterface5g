add_library(nas_common_ies_lib OBJECT
    AuthenticationResponseParameter.c
    NasKeySetIdentifier.c
)

target_include_directories(nas_common_ies_lib PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
)
