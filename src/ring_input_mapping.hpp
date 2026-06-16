#ifndef RING_CONTROL__RING_INPUT_MAPPING_HPP_
#define RING_CONTROL__RING_INPUT_MAPPING_HPP_

#include <algorithm>
#include <cctype>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace ring_control {

inline std::string NormalizeToken(std::string value) {
  std::transform(value.begin(), value.end(), value.begin(),
                 [](unsigned char ch) {
                   if (ch == '-' || ch == ' ') {
                     return '_';
                   }
                   return static_cast<char>(std::tolower(ch));
                 });
  return value;
}

inline const std::unordered_map<std::string, std::string> &SignalMap() {
  static const std::unordered_map<std::string, std::string> kSignalMap = {
      {"swipe_forward", "swipe_forward"},
      {"forward", "swipe_forward"},
      {"swipe_backward", "swipe_backward"},
      {"backward", "swipe_backward"},
      {"swipe_left", "swipe_left"},
      {"left", "swipe_left"},
      {"swipe_right", "swipe_right"},
      {"right", "swipe_right"},
      {"pinch", "pinch"},
      {"stop", "pinch"},
      {"stand", "stand_up"},
      {"stand_up", "stand_up"},
      {"standup", "stand_up"},
      {"up", "stand_up"},
      {"lay_down", "stand_down"},
      {"laydown", "stand_down"},
      {"lie_down", "stand_down"},
      {"liedown", "stand_down"},
      {"down", "stand_down"},
      {"stand_down", "stand_down"},
      {"standdown", "stand_down"},
      {"spin_clockwise", "spin_clockwise"},
      {"clockwise", "spin_clockwise"},
      {"cw", "spin_clockwise"},
      {"spin_counterclockwise", "spin_counterclockwise"},
      {"counterclockwise", "spin_counterclockwise"},
      {"ccw", "spin_counterclockwise"},
  };
  return kSignalMap;
}

inline std::string MapRawSignalToGesture(const std::string &signal) {
  const auto normalized = NormalizeToken(signal);
  const auto &map = SignalMap();
  const auto it = map.find(normalized);
  if (it == map.end()) {
    return {};
  }
  return it->second;
}

inline std::vector<std::pair<std::string, std::string>> SupportedInputDescriptions() {
  return {
      {"forward, swipe_forward", "move forward"},
      {"backward, swipe_backward", "move backward"},
      {"left, swipe_left", "strafe left"},
      {"right, swipe_right", "strafe right"},
      {"pinch, stop", "stop immediately"},
      {"stand, stand_up, up", "stand up from lying down"},
      {"lay_down, lie_down, stand_down, down", "stand down / lie down"},
      {"cw, clockwise, spin_clockwise",
       "turn clockwise, or arc clockwise if already moving"},
      {"ccw, counterclockwise, spin_counterclockwise",
       "turn counterclockwise, or arc counterclockwise if already moving"},
  };
}

}  // namespace ring_control

#endif  // RING_CONTROL__RING_INPUT_MAPPING_HPP_
