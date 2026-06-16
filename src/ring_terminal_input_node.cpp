#include <iostream>
#include <string>

#include <rclcpp/rclcpp.hpp>
#include <std_msgs/msg/string.hpp>

#include "ring_input_mapping.hpp"

namespace {

constexpr char kDefaultRawRingTopic[] = "/ring/raw_signal";

class RingTerminalInputNode : public rclcpp::Node {
public:
  RingTerminalInputNode() : Node("ring_terminal_input_node") {
    declare_parameter("raw_ring_topic", std::string(kDefaultRawRingTopic));
    raw_ring_topic_ = get_parameter("raw_ring_topic").as_string();

    publisher_ = create_publisher<std_msgs::msg::String>(raw_ring_topic_, 10);

    RCLCPP_INFO(get_logger(),
                "Terminal ring-input tester publishing raw ring strings to %s",
                publisher_->get_topic_name());
  }

  void RunInteractiveLoop() {
    PrintHelp();

    std::string line;
    while (rclcpp::ok()) {
      std::cout << "ring-input> " << std::flush;
      if (!std::getline(std::cin, line)) {
        std::cout << "\n";
        break;
      }

      const std::string normalized = ring_control::NormalizeToken(line);
      if (normalized.empty()) {
        continue;
      }

      if (normalized == "help") {
        PrintHelp();
        continue;
      }

      if (normalized == "quit" || normalized == "exit") {
        break;
      }

      const std::string gesture = ring_control::MapRawSignalToGesture(line);
      if (gesture.empty()) {
        std::cout << "Unsupported input: '" << line << "'\n";
        std::cout << "Type 'help' to see supported test inputs.\n";
        continue;
      }

      std_msgs::msg::String message;
      message.data = line;
      publisher_->publish(message);

      std::cout << "Published raw input '" << line << "' -> gesture '" << gesture << "'\n";
    }
  }

private:
  void PrintHelp() const {
    std::cout << "Supported terminal ring inputs:\n";
    for (const auto &entry : ring_control::SupportedInputDescriptions()) {
      std::cout << "  " << entry.first << " : " << entry.second << "\n";
    }
    std::cout << "Other commands:\n";
    std::cout << "  help : show this list\n";
    std::cout << "  quit : exit the tester\n";
  }

  std::string raw_ring_topic_;
  rclcpp::Publisher<std_msgs::msg::String>::SharedPtr publisher_;
};

}  // namespace

int main(int argc, char **argv) {
  rclcpp::init(argc, argv);
  auto node = std::make_shared<RingTerminalInputNode>();
  node->RunInteractiveLoop();
  rclcpp::shutdown();
  return 0;
}
