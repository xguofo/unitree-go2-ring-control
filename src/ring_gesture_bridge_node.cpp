#include <string>

#include <rclcpp/rclcpp.hpp>
#include <std_msgs/msg/string.hpp>

#include "ring_input_mapping.hpp"

namespace {

// Raw strings arrive here from the ring-facing side. Normalized gesture
// strings leave on /ring/gesture for the motion node to consume.
constexpr char kDefaultRawRingTopic[] = "/ring/raw_signal";
constexpr char kDefaultGestureTopic[] = "/ring/gesture";

class RingGestureBridgeNode : public rclcpp::Node {
public:
  RingGestureBridgeNode() : Node("ring_gesture_bridge_node") {
    declare_parameter("raw_ring_topic", std::string(kDefaultRawRingTopic));
    declare_parameter("gesture_topic", std::string(kDefaultGestureTopic));

    raw_ring_topic_ = get_parameter("raw_ring_topic").as_string();
    gesture_topic_ = get_parameter("gesture_topic").as_string();

    // This node only translates between two string topics. It does not send
    // any robot commands directly.
    publisher_ = create_publisher<std_msgs::msg::String>(gesture_topic_, 10);
    subscriber_ = create_subscription<std_msgs::msg::String>(
        raw_ring_topic_, 10,
        [this](const std_msgs::msg::String::SharedPtr msg) {
          handle_raw_signal(msg->data);
        });

    RCLCPP_INFO(get_logger(),
                "Listening for raw ring strings on %s and publishing normalized gestures to %s",
                subscriber_->get_topic_name(),
                publisher_->get_topic_name());
  }

private:
  void handle_raw_signal(const std::string &raw_signal) {
    // Normalize first so variants like "Spin Clockwise" and
    // "spin-clockwise" can share the same mapping entry.
    const std::string gesture = ring_control::MapRawSignalToGesture(raw_signal);
    if (gesture.empty()) {
      RCLCPP_WARN(get_logger(), "Ignoring unknown raw ring signal: '%s'",
                  raw_signal.c_str());
      return;
    }

    // Publish the canonical gesture name expected by ring_control_node.
    std_msgs::msg::String output;
    output.data = gesture;
    publisher_->publish(output);

    RCLCPP_INFO(get_logger(), "Raw ring signal '%s' -> gesture '%s'",
                raw_signal.c_str(), gesture.c_str());
  }
  std::string raw_ring_topic_;
  std::string gesture_topic_;
  rclcpp::Publisher<std_msgs::msg::String>::SharedPtr publisher_;
  rclcpp::Subscription<std_msgs::msg::String>::SharedPtr subscriber_;
};

}  // namespace

int main(int argc, char **argv) {
  rclcpp::init(argc, argv);
  rclcpp::spin(std::make_shared<RingGestureBridgeNode>());
  rclcpp::shutdown();
  return 0;
}
