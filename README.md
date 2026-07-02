# Ring Control

`ring_control` turns ring-style string inputs into Unitree Go2 sport-mode commands.

Data flow:

```text
physical ring TCP stream -> ring_tcp_bridge_node -> /ring/raw_signal -> ring_gesture_bridge_node -> /ring/gesture -> ring_control_node -> /api/sport/request
```

For terminal-only testing:

```text
ring_terminal_input_node -> /ring/raw_signal
```

## Nodes

`ring_control_node`

- Subscribes to `/ring/gesture`.
- Publishes Unitree sport requests to `/api/sport/request`.
- Handles movement, turning, stop, stand up, and stand down.

`ring_gesture_bridge_node`

- Subscribes to `/ring/raw_signal`.
- Maps raw strings like `forward`, `cw`, and `lay_down`.
- Publishes canonical gestures to `/ring/gesture`.

`ring_tcp_bridge_node`

- Connects to the ring TCP server at `127.0.0.1:17888` by default.
- Subscribes to the ring `swipe` stream.
- Publishes each swipe `event` string to `/ring/raw_signal`.

`ring_terminal_input_node`

- Reads test input from the terminal.
- Publishes raw strings to `/ring/raw_signal`.
- Prints supported inputs and warns when an input is unsupported.

## Supported Inputs

| Raw input aliases | Normalized gesture | Behavior |
| --- | --- | --- |
| `forward`, `swipe_forward`, `swipe_up` | `swipe_forward` | Move forward |
| `backward`, `swipe_backward`, `swipe_down` | `swipe_backward` | Move backward |
| `left`, `swipe_left` | `swipe_left` | Strafe left |
| `right`, `swipe_right` | `swipe_right` | Strafe right |
| `pinch`, `stop` | `pinch` | Stop immediately |
| `tap` | `posture_toggle` | Toggle stand down / stand up |
| `stand`, `stand_up`, `standup`, `up` | `stand_up` | Stand up |
| `lay_down`, `laydown`, `lie_down`, `liedown`, `down`, `stand_down`, `standdown` | `stand_down` | Stand down / lie down |
| `cw`, `clockwise`, `spin_clockwise` | `spin_clockwise` | Turn clockwise, or arc clockwise while moving |
| `ccw`, `counterclockwise`, `spin_counterclockwise` | `spin_counterclockwise` | Turn counterclockwise, or arc counterclockwise while moving |

## Motion Behavior

Movement gestures create timed motion segments. A movement gesture lasts for `segment_duration_ms` before stopping or switching to a pending command.

Conflicting movement commands do not replace the current command immediately. Example: `forward` then `left` keeps the forward segment active, stores `left` as pending, pauses briefly, then starts left strafe.

Only one pending translation is stored. If another conflicting translation arrives while one is pending, the newest one replaces the older one.

Repeated same-direction movement refreshes the active segment.

Turn gestures modify the active segment. If the robot is moving, `cw` and `ccw` add yaw so the robot follows an arc. If the robot is idle, they create an in-place turn.

`pinch` or `stop` clears active and pending motion immediately.

If `stand_down` arrives during motion, the current motion segment finishes first. The node then stops, settles, and sends `StandDown`. While stand-down is pending, later movement and turn gestures are ignored.

`tap` is mapped to `posture_toggle`. If the robot is up, `tap` requests stand down. If the robot is already down or is in the middle of standing down, `tap` requests stand up.

While the robot is lying down or standing down, movement and turn gestures are ignored. `tap`, `stand`, and `stand_up` are still accepted so the robot can stand back up.

## Build

Set `UNITREE_ROS2_DIR` to the root of your `unitree_ros2` checkout:

```bash
export UNITREE_ROS2_DIR=~/ros2_ws/unitree_ros2
```

From the example workspace:

```bash
cd "$UNITREE_ROS2_DIR/example"
source "$UNITREE_ROS2_DIR/setup.sh"
colcon build --packages-select ring_control
source install/setup.bash
```

If you are building without a live robot connection, source ROS and the Unitree message workspace directly:

```bash
cd "$UNITREE_ROS2_DIR/example"
source /opt/ros/humble/setup.bash
source "$UNITREE_ROS2_DIR/cyclonedds_ws/install/setup.bash"
colcon build --packages-select ring_control
source install/setup.bash
```

## Check Robot Connection

Before sending real commands:

```bash
cd "$UNITREE_ROS2_DIR"
source ./setup.sh
ros2 topic list
ros2 topic echo --once /sportmodestate
```

Expected topics include:

- `/api/sport/request`
- `/api/sport/response`
- `/sportmodestate`
- `/lf/sportmodestate`

Do not run live control if these topics are not visible.

## Safe Dry Run

Use topic remapping to test motion without publishing to the real robot command topic.

Terminal 1:

```bash
cd "$UNITREE_ROS2_DIR"
source ./setup.sh
source ./example/install/setup.bash
ros2 run ring_control ring_control_node --ros-args -r /api/sport/request:=/ring/test_api_sport_request
```

Terminal 2:

```bash
cd "$UNITREE_ROS2_DIR"
source ./setup.sh
source ./example/install/setup.bash
ros2 run ring_control ring_gesture_bridge_node
```

Terminal 3:

```bash
cd "$UNITREE_ROS2_DIR"
source ./setup.sh
source ./example/install/setup.bash
ros2 run ring_control ring_terminal_input_node
```

Optional monitor:

```bash
source "$UNITREE_ROS2_DIR/setup.sh"
source "$UNITREE_ROS2_DIR/example/install/setup.bash"
ros2 topic echo /ring/test_api_sport_request
```

## Ring TCP Input

Use these terminals when the physical ring is connected and the TCP ring server is running.

Terminal 1:

```bash
cd "$UNITREE_ROS2_DIR"
source ./setup.sh
source ./example/install/setup.bash
ros2 run ring_control ring_control_node
```

Terminal 2:

```bash
cd "$UNITREE_ROS2_DIR"
source ./setup.sh
source ./example/install/setup.bash
ros2 run ring_control ring_gesture_bridge_node
```

Terminal 3:

```bash
cd "$UNITREE_ROS2_DIR"
source ./setup.sh
source ./example/install/setup.bash
ros2 run ring_control ring_tcp_bridge_node
```

If you need manual string tests without ring hardware, use `ring_terminal_input_node` instead of `ring_tcp_bridge_node`.

## Run On The Robot

Use three terminals.

Terminal 1:

```bash
cd "$UNITREE_ROS2_DIR"
source ./setup.sh
source ./example/install/setup.bash
ros2 run ring_control ring_control_node
```

Terminal 2:

```bash
cd "$UNITREE_ROS2_DIR"
source ./setup.sh
source ./example/install/setup.bash
ros2 run ring_control ring_gesture_bridge_node
```

Terminal 3:

```bash
cd "$UNITREE_ROS2_DIR"
source ./setup.sh
source ./example/install/setup.bash
ros2 run ring_control ring_tcp_bridge_node
```

Manual string tests:

```text
help
pinch
swipe_up
swipe_down
left
right
cw
ccw
tap
lay_down
stand_up
```

Keep a manual stop available. Do not run `custom_walk_node` and `ring_control_node` at the same time because both can publish sport commands.

## Wireless Robot-Side Test

If the laptop can reach the robot-side computer over Wi-Fi, a practical cable-free test path is:

```text
laptop / ring app -> Wi-Fi network -> robot-side computer -> robot ethernet side -> /api/sport/request
```

This avoids sending the full ROS 2 sport-control path from the laptop directly over Wi-Fi.

First, find the robot-side Wi-Fi address. On the robot-side computer:

```bash
nmcli device status
ip -brief addr show wlan0
ip route
```

Look for the IPv4 address on `wlan0`, then substitute it below as `ROBOT_WIFI_IP`.

Laptop-side Wi-Fi checks:

```bash
ping -c 3 ROBOT_WIFI_IP
ssh unitree@ROBOT_WIFI_IP
```

If the `ring_control` package is not already on the robot-side workspace, copy it into the robot workspace and build only that package. The exact workspace layout may differ between robots; the example below matches a workspace where the robot-side source tree lives under `~/go2_ros2_ws/src/go2_ros2_toolbox/example`:

```bash
tar -C "$UNITREE_ROS2_DIR/example" -czf - ring_control | \
ssh unitree@ROBOT_WIFI_IP 'mkdir -p ~/go2_ros2_ws/src/go2_ros2_toolbox/example && tar -C ~/go2_ros2_ws/src/go2_ros2_toolbox/example -xzf -'
```

```bash
ssh unitree@ROBOT_WIFI_IP
source /opt/ros/foxy/setup.bash
cd ~/go2_ros2_ws
colcon build --packages-select ring_control
source install/setup.bash
```

For Foxy-based robot-side workspaces, if `ros2 run` fails with a CycloneDDS XML parser error, use this temporary workaround in every terminal before launching nodes:

```bash
unset CYCLONEDDS_URI
```

Wireless terminal-input test from the robot-side computer:

Terminal 1:

```bash
source /opt/ros/foxy/setup.bash
source ~/go2_ros2_ws/install/setup.bash
unset CYCLONEDDS_URI
ros2 run ring_control ring_control_node
```

Terminal 2:

```bash
source /opt/ros/foxy/setup.bash
source ~/go2_ros2_ws/install/setup.bash
unset CYCLONEDDS_URI
ros2 run ring_control ring_gesture_bridge_node
```

Terminal 3:

```bash
source /opt/ros/foxy/setup.bash
source ~/go2_ros2_ws/install/setup.bash
unset CYCLONEDDS_URI
ros2 run ring_control ring_terminal_input_node
```

Before unplugging the laptop Ethernet cable, keep these running:

```bash
ping ROBOT_WIFI_IP
```

```bash
ssh unitree@ROBOT_WIFI_IP
```

Only remove Ethernet after both stay stable.

## Stop And Disconnect

To end a live session cleanly:

1. In the terminal-input tester, send `pinch` or `stop`.
2. Wait for the robot to stop moving completely.
3. Press `Ctrl+C` in `ring_terminal_input_node`.
4. Press `Ctrl+C` in `ring_gesture_bridge_node`.
5. Press `Ctrl+C` in `ring_control_node`.
6. If `ring_tcp_bridge_node` is running, stop it with `Ctrl+C` as well.

If you want to disconnect robot-side Wi-Fi after testing, first list active connections:

```bash
nmcli connection show --active
```

Then disconnect the active Wi-Fi connection by name:

```bash
sudo nmcli connection down YOUR_WIFI_CONNECTION_NAME
```

Then close SSH sessions:

```bash
exit
```

## Parameters

| Parameter | Default | Meaning |
| --- | ---: | --- |
| `gesture_topic` | `/ring/gesture` | Canonical gesture input topic |
| `forward_speed` | `0.25` | Forward speed |
| `backward_speed` | `0.30` | Backward speed |
| `left_speed` | `0.40` | Left strafe speed |
| `right_speed` | `0.27` | Right strafe speed |
| `turn_speed` | `0.7` | Idle spin yaw speed |
| `turn_step` | `0.2` | Yaw increment when turning during movement |
| `max_turn_speed` | `1.0` | Maximum yaw speed |
| `period_ms` | `100` | Motion heartbeat publish period |
| `gesture_timeout_ms` | `2000` | Safety timeout since last accepted gesture |
| `segment_duration_ms` | `1500` | Duration of one motion gesture |
| `settle_duration_ms` | `250` | Pause before switching conflicting translations |
| `motion_cooldown_ms` | `100` | Short input cooldown for noisy ring gestures |
| `posture_transition_ms` | `2500` | Local estimate for stand up/down completion |

## Troubleshooting

No robot topics are visible:

- Check the network connection.
- Source `$UNITREE_ROS2_DIR/setup.sh`.
- Confirm `ros2 topic echo --once /sportmodestate` returns data.

Terminal tester reports unsupported input:

- Type `help`.
- Use one of the aliases in the supported input table.

Robot moves too briefly:

- Increase `segment_duration_ms`.

Left or right strafe is weak:

- Tune `left_speed` or `right_speed`.

Robot ignores motion:

- Check whether `stand_down` is pending.
- Check whether the robot is lying down or in a stand-up / stand-down transition.
- Watch the `ring_control_node` logs.

`ros2 run` fails with a CycloneDDS XML parser error:

- Check whether `CYCLONEDDS_URI` points to a malformed XML file.
- As a quick workaround, run `unset CYCLONEDDS_URI` before launching nodes.

Wireless ping fails even though the hotspot was connected earlier:

- Check whether `wlan0` still exists with `ip -brief addr`.
- Check whether the USB Wi-Fi dongle is still visible with `lsusb`.
- If the dongle disappeared, replug it and reconnect the intended Wi-Fi profile.

Robot-side Wi-Fi scans return no SSIDs or scan commands time out:

- Check `sudo dmesg | tail -n 80`.
- If you see scan timeouts or driver queue crashes, the Wi-Fi dongle or driver is unstable.
- In that case, prefer Ethernet or a different USB Wi-Fi adapter.
