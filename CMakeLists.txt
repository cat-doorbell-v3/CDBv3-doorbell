cmake_minimum_required(VERSION 3.5)
set(EXTRA_COMPONENT_DIRS 
    test_data
    $ENV{WHO_PATH}/components)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(cat_doorbell_v3)
