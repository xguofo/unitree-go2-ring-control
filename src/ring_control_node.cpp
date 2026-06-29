#include <algorithm>
#include <chrono>
#include <cctype>
#include <cmath>
#include <sstream>
#include <string>
#include <utility>

#include <rclcpp/rclcpp.hpp>
#include <std_msgs/msg/string.hpp>
#include <unitree_api/msg/request.hpp>

namespace {

// Unitree sport-mode request topic and API ids used by this node.
constexpr char kRequestTopic[] = "/api/sport/request";
constexpr char kGestureTopic[] = "/ring/gesture";
constexpr int32_t kMoveApiId = 1008;
constexpr int32_t kStopApiId = 1003;
constexpr int32_t kStandUpApiId = 1004;
constexpr int32_t kStandDownApiId = 1005;
constexpr double kZeroEpsilon = 1e-6;

// Simple in-memory motion target that we keep publishing as a heartbeat.
struct MotionCommand {
  double vx{0.0};
  double vy{0.0};
  double vyaw{0.0};

  bool is_zero() const {
    return std::abs(vx) < kZeroEpsilon && std::abs(vy) < kZeroEpsilon &&
           std::abs(vyaw) < kZeroEpsilon;
  }
};

enum class MotionState {
  kIdle,
  kExecuting,
  kSettling,
};

enum class GestureKind {
  kUnknown,
  kStop,
  kTranslation,
  kTurn,
  // Reserved for the next step when posture commands are introduced.
  kPosture,
};

enum class PostureState {
  kStanding,
  kLyingDown,
  kStandingUp,
  kStandingDown,
};

std::string normalize_gesture(std::string value) {
  // Accept case-insensitive gesture names from the ring bridge.
  std::transform(value.begin(), value.end(), value.begin(),
                 [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
  return value;
}

std::pair<bool, MotionCommand> command_for_translation_gesture(
    const std::string &gesture, double forward_speed, double backward_speed,
    double left_speed,
    double right_speed) {
  MotionCommand command;

  if (gesture == "swipe_forward") {
    command.vx = forward_speed;
    return {true, command};
  }

  if (gesture == "swipe_backward") {
    command.vx = -backward_speed;
    return {true, command};
  }

  if (gesture == "swipe_left") {
    command.vy = left_speed;
    return {true, command};
  }

  if (gesture == "swipe_right") {
    command.vy = -right_speed;
    return {true, command};
  }

  return {false, MotionCommand{}};
}

bool same_translation(const MotionCommand &lhs, const MotionCommand &rhs) {
  return std::abs(lhs.vx - rhs.vx) < kZeroEpsilon &&
         std::abs(lhs.vy - rhs.vy) < kZeroEpsilon;
}

class RingControlNode : public rclcpp::Node {
public:
  RingControlNode()
      : Node("ring_control_node"),
        publisher_(create_publisher<unitree_api::msg::Request>(kRequestTopic, 10)) {
    // Motion tuning and integration points are exposed as ROS parameters so
    // the ring bridge and robot behavior can be adjusted without recompiling.
    declare_parameter("gesture_topic", std::string(kGestureTopic));
    declare_parameter("forward_speed", 0.25);
    declare_parameter("backward_speed", 0.30);
    declare_parameter("left_speed", 0.40);
    declare_parameter("right_speed", 0.27);
    declare_parameter("turn_speed", 0.7);
    declare_parameter("turn_step", 0.2);
    declare_parameter("max_turn_speed", 1.0);
    declare_parameter("period_ms", 100);
    declare_parameter("gesture_timeout_ms", 2000);
    declare_parameter("segment_duration_ms", 1500);
    declare_parameter("settle_duration_ms", 250);
    declare_parameter("motion_cooldown_ms", 100);
    declare_parameter("posture_transition_ms", 2500);

    load_parameters();

    gesture_subscriber_ = create_subscription<std_msgs::msg::String>(
        gesture_topic_, 10,
        [this](const std_msgs::msg::String::SharedPtr msg) {
          // This topic is expected to contain canonical gesture names from the
          // bridge node, for example "swipe_forward" or "pinch".
          handle_gesture(normalize_gesture(msg->data));
        });

    const auto period = std::chrono::milliseconds(period_ms_);
    timer_ = create_wall_timer(period, [this]() { on_timer(); });

    RCLCPP_INFO(get_logger(),
                "Listening for ring gestures on %s and publishing sport commands to %s",
                gesture_subscriber_->get_topic_name(),
                publisher_->get_topic_name());
  }

private:
  void load_parameters() {
    // Cache parameters once at startup. This node currently assumes static
    // parameters rather than live parameter updates.
    gesture_topic_ = get_parameter("gesture_topic").as_string();
    forward_speed_ = get_parameter("forward_speed").as_double();
    backward_speed_ = get_parameter("backward_speed").as_double();
    left_speed_ = get_parameter("left_speed").as_double();
    right_speed_ = get_parameter("right_speed").as_double();
    turn_speed_ = get_parameter("turn_speed").as_double();
    turn_step_ = get_parameter("turn_step").as_double();
    max_turn_speed_ = get_parameter("max_turn_speed").as_double();
    period_ms_ = get_parameter("period_ms").as_int();
    gesture_timeout_ms_ = get_parameter("gesture_timeout_ms").as_int();
    segment_duration_ms_ = get_parameter("segment_duration_ms").as_int();
    settle_duration_ms_ = get_parameter("settle_duration_ms").as_int();
    motion_cooldown_ms_ = get_parameter("motion_cooldown_ms").as_int();
    posture_transition_ms_ = get_parameter("posture_transition_ms").as_int();
  }

  void handle_gesture(const std::string &gesture) {
    switch (classify_gesture(gesture)) {
      case GestureKind::kStop:
        handle_stop_gesture(gesture);
        return;
      case GestureKind::kTurn:
        handle_turn_gesture(gesture);
        return;
      case GestureKind::kTranslation:
        handle_translation_gesture(gesture);
        return;
      case GestureKind::kPosture:
        handle_posture_gesture(gesture);
        return;
      case GestureKind::kUnknown:
      default:
        RCLCPP_WARN(get_logger(), "Ignoring unknown ring gesture: '%s'", gesture.c_str());
        return;
    }
  }

  GestureKind classify_gesture(const std::string &gesture) const {
    if (gesture == "pinch") {
      return GestureKind::kStop;
    }

    if (gesture == "spin_clockwise" || gesture == "spin_counterclockwise") {
      return GestureKind::kTurn;
    }

    if (gesture == "stand" || gesture == "stand_up" || gesture == "lay_down" ||
        gesture == "stand_down" || gesture == "posture_toggle") {
      return GestureKind::kPosture;
    }

    const auto [recognized, unused_command] =
        command_for_translation_gesture(gesture, forward_speed_, backward_speed_,
                                        left_speed_, right_speed_);
    static_cast<void>(unused_command);
    return recognized ? GestureKind::kTranslation : GestureKind::kUnknown;
  }

  void handle_stop_gesture(const std::string &gesture) {
    // A stop gesture bypasses all arbitration. It clears both active and
    // pending motion so the robot stops immediately.
    last_gesture_time_ = now();
    last_motion_gesture_ = gesture;
    clear_pending_command();
    stop_motion("pinch");
  }

  void handle_posture_gesture(const std::string &gesture) {
    if (gesture == "posture_toggle") {
      // Tap is mapped here so one gesture can stand the robot up if it is
      // already down, or make it lie down if it is currently up.
      if (posture_state_ == PostureState::kLyingDown ||
          posture_state_ == PostureState::kStandingDown) {
        handle_stand_up_gesture();
      } else {
        handle_stand_down_gesture();
      }
      return;
    }

    if (gesture == "stand" || gesture == "stand_up") {
      handle_stand_up_gesture();
      return;
    }

    if (gesture == "lay_down" || gesture == "stand_down") {
      handle_stand_down_gesture();
      return;
    }

    RCLCPP_WARN(get_logger(), "Ignoring unknown posture gesture: '%s'", gesture.c_str());
  }

  void handle_stand_up_gesture() {
    // Stand-up is the only command that is accepted while lying down. It also
    // clears a queued stand-down request because the latest posture intent is up.
    last_gesture_time_ = now();
    has_pending_stand_down_ = false;

    if (posture_state_ == PostureState::kStanding ||
        posture_state_ == PostureState::kStandingUp) {
      RCLCPP_INFO(get_logger(), "Ignoring stand_up because the robot is already standing");
      return;
    }

    clear_pending_command();
    stop_motion("preparing to stand up");
    publish_simple_sport_command(kStandUpApiId, "StandUp");
    posture_state_ = PostureState::kStandingUp;
    posture_transition_deadline_ = now() + duration_from_ms(posture_transition_ms_);
  }

  void handle_stand_down_gesture() {
    last_gesture_time_ = now();

    if (posture_state_ == PostureState::kLyingDown ||
        posture_state_ == PostureState::kStandingDown) {
      RCLCPP_INFO(get_logger(), "Ignoring stand_down because the robot is already down");
      return;
    }

    if (has_active_motion_ || motion_state_ == MotionState::kSettling) {
      // Do not collapse posture in the middle of a motion segment. Queue one
      // high-priority stand-down request and block later motion gestures.
      has_pending_stand_down_ = true;
      clear_pending_command();
      RCLCPP_INFO(get_logger(),
                  "Queued stand_down; current motion will finish before lying down");
      return;
    }

    perform_stand_down("stand_down");
  }

  void handle_turn_gesture(const std::string &gesture) {
    if (!can_accept_motion_gesture(gesture)) {
      return;
    }

    if (motion_gesture_in_cooldown()) {
      RCLCPP_INFO(get_logger(), "Ignoring turn gesture '%s' during cooldown",
                  gesture.c_str());
      return;
    }

    // While settling we intentionally ignore turns. The settle phase is the
    // pause used before switching into a conflicting pending translation.
    if (motion_state_ == MotionState::kSettling) {
      RCLCPP_INFO(get_logger(), "Ignoring turn gesture '%s' while settling",
                  gesture.c_str());
      return;
    }

    last_gesture_time_ = now();
    last_motion_gesture_ = gesture;
    last_motion_gesture_time_ = now();

    if (gesture == "spin_clockwise") {
      // Clockwise is treated as negative yaw in this mapping.
      apply_turn(-1.0);
    } else {
      // Counterclockwise is treated as positive yaw in this mapping.
      apply_turn(1.0);
    }

    activate_current_command();
    RCLCPP_INFO(get_logger(), "Gesture '%s' -> vx=%.3f vy=%.3f vyaw=%.3f",
                gesture.c_str(), command_.vx, command_.vy, command_.vyaw);
  }

  void handle_translation_gesture(const std::string &gesture) {
    if (!can_accept_motion_gesture(gesture)) {
      return;
    }

    if (motion_gesture_in_cooldown()) {
      RCLCPP_INFO(get_logger(), "Ignoring translation gesture '%s' during cooldown",
                  gesture.c_str());
      return;
    }

    const auto [recognized, command] =
        command_for_translation_gesture(gesture, forward_speed_, backward_speed_,
                                        left_speed_, right_speed_);
    if (!recognized) {
      RCLCPP_WARN(get_logger(), "Ignoring unknown translation gesture: '%s'",
                  gesture.c_str());
      return;
    }

    last_gesture_time_ = now();
    last_motion_gesture_ = gesture;
    last_motion_gesture_time_ = now();

    // While settling, do not interrupt the settle window. We only keep the
    // most recent pending translation so the latest operator intent wins.
    if (motion_state_ == MotionState::kSettling) {
      queue_pending_command(command, "updated pending command while settling");
      return;
    }

    if (!has_active_motion_) {
      // Idle -> begin a fresh motion segment immediately.
      command_ = command;
      activate_current_command();
      RCLCPP_INFO(get_logger(), "Gesture '%s' -> vx=%.3f vy=%.3f vyaw=%.3f",
                  gesture.c_str(), command_.vx, command_.vy, command_.vyaw);
      return;
    }

    if (same_translation(command_, command)) {
      // Repeating the same translation extends the current motion segment
      // instead of queuing a redundant command.
      command_.vx = command.vx;
      command_.vy = command.vy;
      refresh_segment_deadline();
      RCLCPP_INFO(get_logger(),
                  "Gesture '%s' refreshed active segment: vx=%.3f vy=%.3f vyaw=%.3f",
                  gesture.c_str(), command_.vx, command_.vy, command_.vyaw);
      return;
    }

    // Conflicting translations never apply immediately. The robot keeps the
    // current segment, then pauses briefly, then switches to the pending one.
    queue_pending_command(command, "queued conflicting translation");
  }

  bool can_accept_motion_gesture(const std::string &gesture) const {
    if (has_pending_stand_down_) {
      RCLCPP_INFO(get_logger(),
                  "Ignoring motion gesture '%s' because stand_down is pending",
                  gesture.c_str());
      return false;
    }

    if (posture_state_ == PostureState::kLyingDown ||
        posture_state_ == PostureState::kStandingDown) {
      RCLCPP_INFO(get_logger(),
                  "Ignoring motion gesture '%s' because the robot is lying down",
                  gesture.c_str());
      return false;
    }

    if (posture_state_ == PostureState::kStandingUp) {
      RCLCPP_INFO(get_logger(),
                  "Ignoring motion gesture '%s' while stand_up is in progress",
                  gesture.c_str());
      return false;
    }

    return true;
  }

  bool motion_gesture_in_cooldown() const {
    if (last_motion_gesture_time_.nanoseconds() == 0) {
      return false;
    }

    const auto elapsed = now() - last_motion_gesture_time_;
    if (elapsed > duration_from_ms(motion_cooldown_ms_)) {
      return false;
    }

    // A short global cooldown is used because ring inputs can be noisy and may
    // emit several gestures back-to-back. This prevents duplicate triggers and
    // accidental conflicting motions within tens of milliseconds.
    return true;
  }

  void activate_current_command() {
    has_active_motion_ = !command_.is_zero();
    if (!has_active_motion_) {
      motion_state_ = MotionState::kIdle;
      segment_deadline_ = rclcpp::Time(0, 0, RCL_ROS_TIME);
      return;
    }

    motion_state_ = MotionState::kExecuting;
    refresh_segment_deadline();
  }

  void refresh_segment_deadline() {
    segment_deadline_ = now() + duration_from_ms(segment_duration_ms_);
  }

  void queue_pending_command(const MotionCommand &command, const char *reason) {
    pending_command_ = command;
    has_pending_command_ = true;

    // Keep the active segment alive until its current deadline, then the timer
    // will move the node into a short settle phase before switching commands.
    RCLCPP_INFO(get_logger(),
                "%s: pending vx=%.3f vy=%.3f vyaw=%.3f while active vx=%.3f vy=%.3f vyaw=%.3f",
                reason, pending_command_.vx, pending_command_.vy,
                pending_command_.vyaw, command_.vx, command_.vy, command_.vyaw);
  }

  void clear_pending_command() {
    pending_command_ = MotionCommand{};
    has_pending_command_ = false;
  }

  void apply_turn(double direction) {
    if (command_.is_zero()) {
      // If the robot is otherwise stationary, a spin gesture becomes a pure
      // in-place turn at the configured turn speed.
      command_.vx = 0.0;
      command_.vy = 0.0;
      command_.vyaw = direction * turn_speed_;
      return;
    }

    // If the robot is already moving, preserve translation and bias yaw so the
    // robot follows an arc instead of switching to a pure spin.
    command_.vyaw =
        std::clamp(command_.vyaw + direction * turn_step_, -max_turn_speed_, max_turn_speed_);
  }

  void on_timer() {
    update_posture_transition();

    if (has_motion_work() && gesture_timed_out()) {
      // Fail safe: if the ring stops sending gestures, stop the robot and drop
      // any buffered translation command.
      clear_pending_command();
      stop_motion("gesture timeout");
      if (has_pending_stand_down_) {
        perform_stand_down("gesture timeout with pending stand_down");
      }
      return;
    }

    if (motion_state_ == MotionState::kSettling) {
      if (segment_deadline_.nanoseconds() != 0 && now() >= segment_deadline_) {
        if (has_pending_stand_down_) {
          perform_stand_down("pending stand_down after settle");
        } else {
          start_pending_command_if_any();
        }
      }
      return;
    }

    if (!has_active_motion_) {
      if (has_pending_stand_down_) {
        perform_stand_down("pending stand_down after motion stopped");
      }
      return;
    }

    if (motion_state_ == MotionState::kExecuting &&
        segment_deadline_.nanoseconds() != 0 && now() >= segment_deadline_) {
      if (has_pending_command_) {
        begin_settle_phase();
      } else {
        stop_motion("segment complete");
        if (has_pending_stand_down_) {
          perform_stand_down("pending stand_down after segment complete");
        }
      }
      return;
    }

    publish_current_command();
  }

  void begin_settle_phase() {
    // Conflicting translations do not swap instantly. The settle phase inserts
    // a short pause so the dog is less likely to snap from one motion to the
    // next like a wheeled robot would.
    stop_motion("settling before pending translation", false);
    motion_state_ = MotionState::kSettling;
    segment_deadline_ = now() + duration_from_ms(settle_duration_ms_);
  }

  void perform_stand_down(const std::string &reason) {
    // StandDown is a posture command, not a movement command. We clear motion
    // first so no velocity heartbeat resumes while the dog is going down.
    has_pending_stand_down_ = false;
    clear_pending_command();
    stop_motion("preparing to stand down");
    publish_simple_sport_command(kStandDownApiId, "StandDown");
    posture_state_ = PostureState::kStandingDown;
    posture_transition_deadline_ = now() + duration_from_ms(posture_transition_ms_);
    RCLCPP_INFO(get_logger(), "Started stand_down: %s", reason.c_str());
  }

  void update_posture_transition() {
    if (posture_transition_deadline_.nanoseconds() == 0 ||
        now() < posture_transition_deadline_) {
      return;
    }

    if (posture_state_ == PostureState::kStandingUp) {
      posture_state_ = PostureState::kStanding;
      RCLCPP_INFO(get_logger(), "StandUp transition complete");
    } else if (posture_state_ == PostureState::kStandingDown) {
      posture_state_ = PostureState::kLyingDown;
      RCLCPP_INFO(get_logger(), "StandDown transition complete");
    }

    posture_transition_deadline_ = rclcpp::Time(0, 0, RCL_ROS_TIME);
  }

  void start_pending_command_if_any() {
    if (!has_pending_command_) {
      motion_state_ = MotionState::kIdle;
      segment_deadline_ = rclcpp::Time(0, 0, RCL_ROS_TIME);
      return;
    }

    command_ = pending_command_;
    clear_pending_command();
    activate_current_command();

    RCLCPP_INFO(get_logger(),
                "Started pending translation after settle: vx=%.3f vy=%.3f vyaw=%.3f",
                command_.vx, command_.vy, command_.vyaw);
  }

  void publish_current_command() {
    // Unitree high-level motion commands behave like a heartbeat, so keep
    // re-publishing the current target while motion is active.
    unitree_api::msg::Request request;
    request.header.identity.api_id = kMoveApiId;
    request.parameter = make_move_payload(command_);
    publisher_->publish(request);
  }

  void publish_simple_sport_command(int32_t api_id, const char *name) {
    unitree_api::msg::Request request;
    request.header.identity.api_id = api_id;
    publisher_->publish(request);

    RCLCPP_INFO(get_logger(), "Published %s command", name);
  }

  bool gesture_timed_out() const {
    if (last_gesture_time_.nanoseconds() == 0) {
      return false;
    }

    // Convert the timeout parameter to a ROS duration and compare it against
    // the time since the last accepted gesture was received.
    const auto elapsed = now() - last_gesture_time_;
    return elapsed > duration_from_ms(gesture_timeout_ms_);
  }

  bool has_motion_work() const {
    return has_active_motion_ || has_pending_command_ || has_pending_stand_down_ ||
           motion_state_ == MotionState::kSettling;
  }

  rclcpp::Duration duration_from_ms(int64_t milliseconds) const {
    return rclcpp::Duration::from_nanoseconds(
        std::chrono::milliseconds(milliseconds).count() * 1000000LL);
  }

  std::string make_move_payload(const MotionCommand &command) const {
    // Unitree Move requests expect a JSON payload with x/y/z meaning
    // forward, lateral, and yaw velocity.
    std::ostringstream payload;
    payload << "{\"x\":" << command.vx << ",\"y\":" << command.vy
            << ",\"z\":" << command.vyaw << "}";
    return payload.str();
  }

  void stop_motion(const std::string &reason, bool clear_active_command = true) {
    // Reset internal state first so the timer does not immediately resume
    // publishing the previous motion target.
    if (clear_active_command) {
      command_ = MotionCommand{};
      motion_state_ = MotionState::kIdle;
      segment_deadline_ = rclcpp::Time(0, 0, RCL_ROS_TIME);
    }
    has_active_motion_ = false;

    // Use the dedicated StopMove API rather than Move(0, 0, 0).
    unitree_api::msg::Request request;
    request.header.identity.api_id = kStopApiId;
    publisher_->publish(request);

    RCLCPP_INFO(get_logger(), "Stopping motion: %s", reason.c_str());
  }

  std::string gesture_topic_;
  double forward_speed_{0.25};
  double backward_speed_{0.30};
  double left_speed_{0.40};
  double right_speed_{0.27};
  double turn_speed_{0.7};
  double turn_step_{0.2};
  double max_turn_speed_{1.0};
  int64_t period_ms_{100};
  int64_t gesture_timeout_ms_{2000};
  int64_t segment_duration_ms_{1500};
  int64_t settle_duration_ms_{250};
  int64_t motion_cooldown_ms_{100};
  int64_t posture_transition_ms_{2500};
  bool has_active_motion_{false};
  bool has_pending_command_{false};
  bool has_pending_stand_down_{false};
  MotionState motion_state_{MotionState::kIdle};
  PostureState posture_state_{PostureState::kStanding};
  MotionCommand command_{};
  MotionCommand pending_command_{};
  std::string last_motion_gesture_;
  // Timestamp of the most recent accepted gesture used by the watchdog timeout.
  rclcpp::Time last_gesture_time_{0, 0, RCL_ROS_TIME};
  // Timestamp of the most recent accepted motion/turn gesture used by the
  // short cooldown that filters noisy ring inputs.
  rclcpp::Time last_motion_gesture_time_{0, 0, RCL_ROS_TIME};
  // Active deadline while executing a motion segment, or settle deadline while
  // transitioning between conflicting translation segments.
  rclcpp::Time segment_deadline_{0, 0, RCL_ROS_TIME};
  // Approximate completion time for StandUp / StandDown. The Unitree API used
  // here is fire-and-forget, so this local state is conservative bookkeeping.
  rclcpp::Time posture_transition_deadline_{0, 0, RCL_ROS_TIME};
  rclcpp::Publisher<unitree_api::msg::Request>::SharedPtr publisher_;
  rclcpp::Subscription<std_msgs::msg::String>::SharedPtr gesture_subscriber_;
  rclcpp::TimerBase::SharedPtr timer_;
};

}  // namespace

int main(int argc, char **argv) {
  rclcpp::init(argc, argv);
  rclcpp::spin(std::make_shared<RingControlNode>());
  rclcpp::shutdown();
  return 0;
}
