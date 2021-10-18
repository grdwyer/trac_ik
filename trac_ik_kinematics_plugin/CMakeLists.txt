cmake_minimum_required(VERSION 3.10.2)
project(trac_ik_kinematics_plugin)

find_package(moveit_common REQUIRED)
moveit_package()

find_package(ament_cmake REQUIRED)
find_package(Eigen3 REQUIRED)
find_package(orocos_kdl REQUIRED)
find_package(tf2_kdl REQUIRED)
find_package(kdl_parser REQUIRED)
find_package(rclcpp REQUIRED)
find_package(random_numbers REQUIRED)
find_package(class_loader REQUIRED)
find_package(pluginlib REQUIRED)
find_package(moveit_core REQUIRED)
find_package(moveit_ros_planning REQUIRED)
find_package(moveit_msgs REQUIRED)
find_package(trac_ik_lib REQUIRED)

# Finds Boost Components
#include(ConfigExtras.cmake)

include_directories(
  include
)

pluginlib_export_plugin_description_file(moveit_core trac_ik_plugin_description.xml)

set(TRAC_IK_LIBRARY_NAME trac_ik_kinematics_plugin)

add_library(${TRAC_IK_LIBRARY_NAME} src/trac_ik_kinematics_plugin.cpp)
#target_link_libraries(${TRAC_IK_LIBRARY_NAME} ${catkin_LIBRARIES} ${pkg_nlopt_LIBRARIES})

ament_target_dependencies(${TRAC_IK_LIBRARY_NAME}
        moveit_core
        pluginlib
        rclcpp
        tf2_kdl
        trac_ik_lib
        )

install(DIRECTORY include/ DESTINATION include)

install(TARGETS ${TRAC_IK_LIBRARY_NAME}
        DESTINATION lib/${PROJECT_NAME})
