cmake_minimum_required(VERSION 3.16)
project(ovs_fc_topology LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

find_package(Qt6 REQUIRED COMPONENTS Core Widgets Network)
find_library(PCAP_LIBRARY pcap REQUIRED)

add_executable(fc_agent
    agent/main.cpp
    agent/switchagent.cpp
    shared/fcdiscovery.cpp
    shared/rtpslite.cpp
)
target_include_directories(fc_agent PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(fc_agent PRIVATE Qt6::Core Qt6::Network ${PCAP_LIBRARY})

add_executable(fc_topology_center
    centre/main.cpp
    centre/mainwindow.cpp
    centre/topologynodeitem.cpp
    centre/topologylinkitem.cpp
    shared/rtpslite.cpp
)
target_include_directories(fc_topology_center PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(fc_topology_center PRIVATE Qt6::Core Qt6::Widgets Qt6::Network)
