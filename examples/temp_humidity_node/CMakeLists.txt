cmake_minimum_required(VERSION 3.16)

set(EXTRA_COMPONENT_DIRS "${CMAKE_CURRENT_LIST_DIR}/../../components")

# local alona_config.h is gitignored; seed from example on first configure so idf.py build works
if(NOT EXISTS "${CMAKE_CURRENT_LIST_DIR}/main/alona_config.h")
    configure_file(
        "${CMAKE_CURRENT_LIST_DIR}/main/alona_config.h.example"
        "${CMAKE_CURRENT_LIST_DIR}/main/alona_config.h"
        COPYONLY)
endif()

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(alona_temp_humidity_node)
