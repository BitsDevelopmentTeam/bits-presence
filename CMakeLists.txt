
cmake_minimum_required(VERSION 2.6)
project(BITS_PRESENCE)

set(CMAKE_BUILD_TYPE Release)

## Targets
set(BITS_SRCS bits_presence.cpp optimistic.cpp)
add_executable(bits_presence ${BITS_SRCS})

## Link libraries
find_package(PNG REQUIRED)
include_directories(${PNG_INCLUDE_DIR})
target_link_libraries(bits_presence ${PNG_LIBRARY})
set(BOOST_LIBS system date_time program_options)
find_package(Boost COMPONENTS ${BOOST_LIBS} REQUIRED)
target_link_libraries(bits_presence ${Boost_LIBRARIES})
find_library(MYSQLPP_LIB mysqlpp)
target_link_libraries(bits_presence ${MYSQLPP_LIB})
find_path(MYSQLPP_INCLUDE_PATH mysql++.h /usr/include/mysql++/)
find_path(MYSQL_INCLUDE_PATH mysql.h /usr/include/mysql/)
include_directories(${MYSQLPP_INCLUDE_PATH} ${MYSQL_INCLUDE_PATH})
