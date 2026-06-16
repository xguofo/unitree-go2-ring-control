# Ring Control

`ring_control` turns ring-style string inputs into Unitree Go2 sport-mode commands.

The intended data flow is:

```text
raw ring string -> ring_gesture_bridge_node -> normalized gesture -> ring_control_node -> /api/sport/request
```

For testing without ring hardware, use:

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
- Maps raw strings such as `forward`, `cw`, and `lay_down`.
- Publishes canonical gestures to `/ring/gesture`.

`ring_terminal_input_node`

- Reads test input from the terminal.
- Publishes raw strings to `/ring/raw_signal`.
- Prints supported inputs and warns when an input is unsupported.

## Supported Inputs

| Raw input aliases | Normalized gesture | Behavior |
| --- | --- | --- |
| `forward`, `swipe_forward` | `swipe_forward` | Move forward |
| `backward`, `swipe_backward` | `swipe_backward` | Move backward |
| `left`, `swipe_left` | `swipe_left` | Strafe left |
| `right`, `swipe_right` | `swipe_right` | Strafe right |
| `pinch`, `stop` | `pinch` | Stop immediately |
| `stand`, `stand_up`, `standup`, `up` | `stand_up` | Stand up |
| `lay_down`, `laydown`, `lie_down`, `liedown`, `down`, `stand_down`, `standdown` | `stand_down` | Stand down / lie down |
| `cw`, `clockwise`, `spin_clockwise` | `spin_clockwise` | Turn clockwise, or arc clockwise while moving |
| `ccw`, `counterclockwise`, `spin_counterclockwise` | `spin_counterclockwise` | Turn counterclockwise, or arc counterclockwise while moving |

## Motion Behavior

Movement gestures create timed motion segments. A single movement gesture lasts for `segment_duration_ms` before stopping or switching to a pending command.

Conflicting movement commands do not replace the current command immediately. For example, `forward` followed by `left` keeps the forward segment active, stores `left` as the pending translation, stops briefly during the settle phase, then starts left strafe.

Only one pending translation is stored. If another conflicting translation arrives while one is pending, the newest pending command replaces the older one.

Repeated same-direction movement refreshes the active segment.

Turn gestures modify the active segment. If the robot is moving, `cw` and `ccw` add yaw so the robot follows an arc. If the robot is idle, they create an in-place turn.

`pinch` or `stop` clears active and pending motion immediately.

If `stand_down` arrives during motion, the current motion segment finishes first. The node then stops/settles and sends `StandDown`. While stand-down is pending, later movement and turn gestures are ignored.

While the robot is lying down or standing down, movement and turn gestures are ignored. `stand` / `stand_up` is accepted while lying down.

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

Use a topic remap to test the full pipeline without publishing to the robot command topic.

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
ros2 run ring_control ring_terminal_input_node
```

Start with a conservative test sequence:

```text
help
stop
forward
stop
left
stop
right
stop
cw
stop
forward
left
stop
lay_down
stand
stop
```

Keep a manual stop available. Do not run `custom_walk_node` and `ring_control_node` at the same time because both can publish sport commands.

## Parameters

| Parameter | Default | Meaning |
| --- | ---: | --- |
| `gesture_topic` | `/ring/gesture` | Canonical gesture input topic |
| `forward_speed` | `0.25` | Forward/backward speed magnitude |
| `left_speed` | `0.32` | Left strafe speed |
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

Common tuning:

- Increase `segment_duration_ms` if one movement gesture is too short.
- Adjust `left_speed` and `right_speed` if one strafe direction is weaker.
- Adjust `forward_speed` for forward/backward speed.
- Adjust `turn_speed` or `turn_step` for spin and arc turning.
- Keep `gesture_timeout_ms` as a safety timeout, not as the normal action duration.

## Troubleshooting

No robot topics are visible:

- Check Wi-Fi or Ethernet connection.
- Source `$UNITREE_ROS2_DIR/setup.sh`.
- Confirm `ros2 topic echo --once /sportmodestate` returns data.

Terminal tester reports unsupported input:

- Type `help`.
- Use one of the aliases listed in the supported input table.

Robot moves too briefly:

- Increase `segment_duration_ms`.

Left or right strafe is weak:

- Tune `left_speed` or `right_speed`.

Robot ignores motion:

- Check whether `stand_down` is pending.
- Check whether the robot is lying down or in a stand-up / stand-down transition.
- Watch the `ring_control_node` logs.
