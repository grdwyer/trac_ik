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
find_package(NLopt REQUIRED)

# Finds Boost Components
#include(ConfigExtras.cmake)

include_directories(
  include
)

pluginlib_export_plugin_description_file(trac_ik_kinematics_plugin trac_ik_plugin_description.xml)

set(TRAC_IK_LIBRARY_NAME trac_ik_kinematics_plugin)

add_compile_options(-fPIC)
add_library(${TRAC_IK_LIBRARY_NAME} SHARED src/trac_ik_kinematics_plugin.cpp)

ament_target_dependencies(${TRAC_IK_LIBRARY_NAME}
        rclcpp
        random_numbers
        pluginlib
        moveit_core
        moveit_msgs
        orocos_kdl
        kdl_parser
        tf2_kdl
        EIGEN3
        trac_ik_lib
        )
# prevent pluginlib from using boost
target_compile_definitions(${TRAC_IK_LIBRARY_NAME} PUBLIC "PLUGINLIB__DISABLE_BOOST_FUNCTIONS")
# Causes the visibility macros to use dllexport rather than dllimport,
# which is appropriate when building the dll but not consuming it.
target_compile_definitions(${TRAC_IK_LIBRARY_NAME} PRIVATE "TRAC_IK_KINEMATICS_PLUGIN_BUILDING_DLL")

install(DIRECTORY include/ DESTINATION include)

install(TARGETS ${TRAC_IK_LIBRARY_NAME}
        DESTINATION lib/${PROJECT_NAME})

install(
        TARGETS ${TRAC_IK_LIBRARY_NAME}
        EXPORT export_${PROJECT_NAME}
        LIBRARY DESTINATION lib
        ARCHIVE DESTINATION lib
        RUNTIME DESTINATION bin
        INCLUDES DESTINATION include
)

ament_export_libraries(
        ${TRAC_IK_LIBRARY_NAME}
)
ament_export_targets(export_${PROJECT_NAME} HAS_LIBRARY_TARGET)
ament_export_dependencies(
        pluginlib
        moveit_core
        moveit_ros_planning
        Boost
        trac_ik_lib
        )

ament_package()