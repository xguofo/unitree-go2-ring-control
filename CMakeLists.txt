cmake_minimum_required(VERSION 3.8)
project(ring_control)

if(NOT CMAKE_CXX_STANDARD)
  set(CMAKE_CXX_STANDARD 17)
endif()

if(CMAKE_COMPILER_IS_GNUCXX OR CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  add_compile_options(-Wall -Wextra -Wpedantic)
endif()

find_package(ament_cmake REQUIRED)
find_package(rclcpp REQUIRED)
find_package(std_msgs REQUIRED)
find_package(unitree_api REQUIRED)

add_executable(ring_control_node
  src/ring_control_node.cpp
)

add_executable(ring_gesture_bridge_node
  src/ring_gesture_bridge_node.cpp
)

add_executable(ring_terminal_input_node
  src/ring_terminal_input_node.cpp
)

add_executable(ring_tcp_bridge_node
  src/ring_tcp_bridge_node.cpp
)

ament_target_dependencies(ring_control_node
  rclcpp
  std_msgs
  unitree_api
)

ament_target_dependencies(ring_gesture_bridge_node
  rclcpp
  std_msgs
)

ament_target_dependencies(ring_terminal_input_node
  rclcpp
  std_msgs
)

ament_target_dependencies(ring_tcp_bridge_node
  rclcpp
  std_msgs
)

install(TARGETS
  ring_control_node
  ring_gesture_bridge_node
  ring_terminal_input_node
  ring_tcp_bridge_node
  DESTINATION lib/${PROJECT_NAME}
)

if(BUILD_TESTING)
  find_package(ament_lint_auto REQUIRED)
  set(ament_cmake_copyright_FOUND TRUE)
  set(ament_cmake_cpplint_FOUND TRUE)
  ament_lint_auto_find_test_dependencies()
endif()

ament_package()
