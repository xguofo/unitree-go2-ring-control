#include <atomic>
#include <chrono>
#include <cerrno>
#include <cstring>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include <arpa/inet.h>
#include <netdb.h>
#include <sys/socket.h>
#include <unistd.h>

#include <rclcpp/rclcpp.hpp>
#include <std_msgs/msg/string.hpp>

namespace {

constexpr char kDefaultRawRingTopic[] = "/ring/raw_signal";

std::string json_escape(const std::string &value) {
  std::ostringstream escaped;
  for (const char ch : value) {
    switch (ch) {
      case '\\':
        escaped << "\\\\";
        break;
      case '"':
        escaped << "\\\"";
        break;
      case '\n':
        escaped << "\\n";
        break;
      case '\r':
        escaped << "\\r";
        break;
      case '\t':
        escaped << "\\t";
        break;
      default:
        escaped << ch;
        break;
    }
  }
  return escaped.str();
}

std::string make_json_string_array(const std::vector<std::string> &values) {
  std::ostringstream payload;
  payload << "[";
  for (std::size_t index = 0; index < values.size(); ++index) {
    if (index > 0) {
      payload << ",";
    }
    payload << "\"" << json_escape(values[index]) << "\"";
  }
  payload << "]";
  return payload.str();
}

std::string make_subscribe_command(const std::vector<std::string> &rings) {
  std::ostringstream payload;
  payload << "{\"cmd\":\"subscribe\",\"rings\":" << make_json_string_array(rings)
          << ",\"streams\":[\"swipe\"]}";
  return payload.str();
}

bool send_line(int socket_fd, const std::string &line) {
  const std::string payload = line + "\n";
  const char *data = payload.data();
  std::size_t remaining = payload.size();

  while (remaining > 0) {
    const ssize_t sent = ::send(socket_fd, data, remaining, MSG_NOSIGNAL);
    if (sent <= 0) {
      return false;
    }
    data += sent;
    remaining -= static_cast<std::size_t>(sent);
  }

  return true;
}

bool json_string_field(const std::string &json, const std::string &key,
                       std::string *value) {
  const std::string quoted_key = "\"" + key + "\"";
  const std::size_t key_pos = json.find(quoted_key);
  if (key_pos == std::string::npos) {
    return false;
  }

  const std::size_t colon_pos = json.find(':', key_pos + quoted_key.size());
  if (colon_pos == std::string::npos) {
    return false;
  }

  std::size_t start = json.find('"', colon_pos + 1);
  if (start == std::string::npos) {
    return false;
  }
  ++start;

  std::ostringstream decoded;
  bool escaped = false;
  for (std::size_t index = start; index < json.size(); ++index) {
    const char ch = json[index];
    if (escaped) {
      switch (ch) {
        case 'n':
          decoded << '\n';
          break;
        case 'r':
          decoded << '\r';
          break;
        case 't':
          decoded << '\t';
          break;
        default:
          decoded << ch;
          break;
      }
      escaped = false;
      continue;
    }

    if (ch == '\\') {
      escaped = true;
      continue;
    }

    if (ch == '"') {
      *value = decoded.str();
      return true;
    }

    decoded << ch;
  }

  return false;
}

class RingTcpBridgeNode : public rclcpp::Node {
public:
  RingTcpBridgeNode() : Node("ring_tcp_bridge_node") {
    declare_parameter("host", std::string("127.0.0.1"));
    declare_parameter("port", 17888);
    declare_parameter("raw_ring_topic", std::string(kDefaultRawRingTopic));
    declare_parameter("ring_addresses", std::vector<std::string>{});
    declare_parameter("reconnect_interval_ms", 2000);

    host_ = get_parameter("host").as_string();
    port_ = get_parameter("port").as_int();
    raw_ring_topic_ = get_parameter("raw_ring_topic").as_string();
    ring_addresses_ = get_parameter("ring_addresses").as_string_array();
    reconnect_interval_ms_ = get_parameter("reconnect_interval_ms").as_int();

    publisher_ = create_publisher<std_msgs::msg::String>(raw_ring_topic_, 10);
    worker_ = std::thread([this]() { run_client_loop(); });

    RCLCPP_INFO(get_logger(),
                "Connecting to ring stream at %s:%ld and publishing raw events to %s",
                host_.c_str(), port_, publisher_->get_topic_name());
  }

  ~RingTcpBridgeNode() override {
    running_.store(false);
    close_active_socket();
    if (worker_.joinable()) {
      worker_.join();
    }
  }

private:
  void run_client_loop() {
    while (rclcpp::ok() && running_.load()) {
      const int socket_fd = connect_to_server();
      if (socket_fd < 0) {
        sleep_before_reconnect();
        continue;
      }

      active_socket_.store(socket_fd);
      RCLCPP_INFO(get_logger(), "Connected to ring stream server");

      // Ask for current ring status for logging, then subscribe only to swipe
      // events. The existing ROS bridge handles gesture name normalization.
      if (!send_line(socket_fd, "{\"cmd\":\"status\"}") ||
          !send_line(socket_fd, make_subscribe_command(ring_addresses_))) {
        RCLCPP_WARN(get_logger(), "Failed to send ring stream subscription");
        close_socket(socket_fd);
        active_socket_.store(-1);
        sleep_before_reconnect();
        continue;
      }

      read_messages(socket_fd);
      close_socket(socket_fd);
      active_socket_.store(-1);

      if (running_.load()) {
        RCLCPP_WARN(get_logger(), "Ring stream disconnected; reconnecting");
        sleep_before_reconnect();
      }
    }
  }

  int connect_to_server() const {
    addrinfo hints{};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    addrinfo *results = nullptr;
    const std::string port_text = std::to_string(port_);
    const int status = ::getaddrinfo(host_.c_str(), port_text.c_str(), &hints, &results);
    if (status != 0) {
      RCLCPP_WARN(get_logger(), "Ring stream address lookup failed: %s",
                  gai_strerror(status));
      return -1;
    }

    int socket_fd = -1;
    for (addrinfo *item = results; item != nullptr; item = item->ai_next) {
      socket_fd = ::socket(item->ai_family, item->ai_socktype, item->ai_protocol);
      if (socket_fd < 0) {
        continue;
      }

      if (::connect(socket_fd, item->ai_addr, item->ai_addrlen) == 0) {
        break;
      }

      close_socket(socket_fd);
      socket_fd = -1;
    }

    ::freeaddrinfo(results);
    if (socket_fd < 0) {
      RCLCPP_WARN(get_logger(), "Could not connect to ring stream at %s:%ld",
                  host_.c_str(), port_);
    }
    return socket_fd;
  }

  void read_messages(int socket_fd) {
    std::string buffer;
    char chunk[4096];

    while (rclcpp::ok() && running_.load()) {
      const ssize_t received = ::recv(socket_fd, chunk, sizeof(chunk), 0);
      if (received <= 0) {
        return;
      }

      buffer.append(chunk, static_cast<std::size_t>(received));
      std::size_t newline_pos = std::string::npos;
      while ((newline_pos = buffer.find('\n')) != std::string::npos) {
        const std::string line = buffer.substr(0, newline_pos);
        buffer.erase(0, newline_pos + 1);
        handle_json_line(line);
      }
    }
  }

  void handle_json_line(const std::string &line) {
    std::string type;
    if (!json_string_field(line, "type", &type)) {
      return;
    }

    if (type == "hello" || type == "status" || type == "subscribed") {
      RCLCPP_DEBUG(get_logger(), "Ring stream message: %s", line.c_str());
      return;
    }

    if (type != "swipe") {
      return;
    }

    std::string event;
    if (!json_string_field(line, "event", &event) || event.empty()) {
      RCLCPP_WARN(get_logger(), "Ignoring swipe message without event: %s",
                  line.c_str());
      return;
    }

    std_msgs::msg::String output;
    output.data = event;
    publisher_->publish(output);

    // Keep this at info level during early robot testing so the operator can
    // verify the ring server event name before the motion bridge maps it.
    RCLCPP_INFO(get_logger(), "Ring swipe event -> raw signal '%s'", event.c_str());
  }

  void sleep_before_reconnect() const {
    const auto deadline =
        std::chrono::steady_clock::now() + std::chrono::milliseconds(reconnect_interval_ms_);
    while (running_.load() && std::chrono::steady_clock::now() < deadline) {
      std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
  }

  void close_active_socket() {
    const int socket_fd = active_socket_.exchange(-1);
    if (socket_fd >= 0) {
      ::shutdown(socket_fd, SHUT_RDWR);
      close_socket(socket_fd);
    }
  }

  static void close_socket(int socket_fd) {
    if (socket_fd >= 0) {
      ::close(socket_fd);
    }
  }

  std::string host_;
  int64_t port_{17888};
  std::string raw_ring_topic_;
  std::vector<std::string> ring_addresses_;
  int64_t reconnect_interval_ms_{2000};
  std::atomic<bool> running_{true};
  std::atomic<int> active_socket_{-1};
  std::thread worker_;
  rclcpp::Publisher<std_msgs::msg::String>::SharedPtr publisher_;
};

}  // namespace

int main(int argc, char **argv) {
  rclcpp::init(argc, argv);
  rclcpp::spin(std::make_shared<RingTcpBridgeNode>());
  rclcpp::shutdown();
  return 0;
}
