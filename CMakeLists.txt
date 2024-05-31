# The following lines of boilerplate have to be in your project's
# CMakeLists in this exact order for cmake to work correctly
# cmake_minimum_required(VERSION 3.5)
# set(EXTRA_COMPONENT_DIRS $ENV{WHO_PATH}/components)
# add_compile_options(-fdiagnostics-color=always)
# include($ENV{IDF_PATH}/tools/cmake/project.cmake)
# project(cat_face_detection_terminal)


cmake_minimum_required(VERSION 3.5)

# Define additional component directories
set(
    EXTRA_COMPONENT_DIRS 
    $ENV{WHO_PATH}/components
    $ENV{IDF_PATH}/components
    "components/config_manager" 
)

# Include the ESP-IDF project cmake file
include($ENV{IDF_PATH}/tools/cmake/project.cmake)

get_filename_component(ROOT_DIR_NAME ${CMAKE_SOURCE_DIR} NAME)
project(${ROOT_DIR_NAME})

idf_build_set_property(COMPILE_DEFINITIONS "-DMY_PROJECT_NAME=\"${PROJECT_NAME}\"" APPEND)
# idf_build_set_property(COMPILE_OPTIONS "-Werror" APPEND)
# idf_build_set_property(COMPILE_OPTIONS "-Wunused" APPEND)

