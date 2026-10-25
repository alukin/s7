# SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
# SPDX-License-Identifier: LGPL-3.0-or-later

# Available to SDK consumers as well as in-tree modules. Reconfigure when the
# resource changes; deployed code never depends on the original file location.
function(s7_embed_metadata target resource symbol)
    get_filename_component(resource "${resource}" ABSOLUTE BASE_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${resource}")
    file(READ "${resource}" S7_METADATA_JSON)
    string(FIND "${S7_METADATA_JSON}" ")s7metadata\"" delimiter_collision)
    if (NOT delimiter_collision EQUAL -1)
        message(FATAL_ERROR "Metadata resource contains the reserved raw-string delimiter: ${resource}")
    endif()
    set(S7_METADATA_SYMBOL "${symbol}")
    set(generated_dir "${CMAKE_CURRENT_BINARY_DIR}/metadata-generated")
    configure_file("${CMAKE_CURRENT_FUNCTION_LIST_DIR}/metadata_resource.hpp.in" "${generated_dir}/${symbol}.hpp" @ONLY)
    target_include_directories(${target} PRIVATE "${generated_dir}")
endfunction()
