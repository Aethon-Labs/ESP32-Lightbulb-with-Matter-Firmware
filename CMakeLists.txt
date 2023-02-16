cmake_minimum_required(VERSION 3.5)

# Set the project name and version
project(lightbulb-matter-firmware VERSION 1.0.0)

# Include ESP-IDF functions
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(${CMAKE_PROJECT_NAME} C CXX)

# Add Matter library
set(MATTER_PATH "path/to/matter/library")
add_subdirectory(${MATTER_PATH} ${CMAKE_BINARY_DIR}/matter)

# Add your source files here
set(SOURCES
    main.cpp
    # add more files here
)

# Create the executable
add_executable(${CMAKE_PROJECT_NAME} ${SOURCES})

# Link the executable with the Matter library
target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE matter)

# Add any additional components needed by your firmware
# Example: `idf_component_register(SRCS "my_component.cpp" INCLUDE_DIRS "include")`

