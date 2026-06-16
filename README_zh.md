# Ring Control 中文说明

`ring_control` 用来把戒指一类设备输出的字符串输入转换成 Unitree Go2 的 sport mode 控制指令。

数据流程如下：

```text
戒指原始字符串 -> ring_gesture_bridge_node -> 标准手势 -> ring_control_node -> /api/sport/request
```

如果没有真实戒指硬件，可以用终端测试节点：

```text
ring_terminal_input_node -> /ring/raw_signal
```

## 节点说明

`ring_control_node`

- 订阅 `/ring/gesture`。
- 发布 Unitree sport 请求到 `/api/sport/request`。
- 负责移动、转向、停止、站起、趴下。

`ring_gesture_bridge_node`

- 订阅 `/ring/raw_signal`。
- 把 `forward`、`cw`、`lay_down` 等原始字符串转换成标准手势。
- 发布标准手势到 `/ring/gesture`。

`ring_terminal_input_node`

- 从终端读取测试输入。
- 发布原始字符串到 `/ring/raw_signal`。
- 启动时会打印支持的输入；不支持的输入会提示，不会发布。

## 支持的输入

| 原始输入别名 | 标准手势 | 行为 |
| --- | --- | --- |
| `forward`, `swipe_forward` | `swipe_forward` | 前进 |
| `backward`, `swipe_backward` | `swipe_backward` | 后退 |
| `left`, `swipe_left` | `swipe_left` | 向左平移 |
| `right`, `swipe_right` | `swipe_right` | 向右平移 |
| `pinch`, `stop` | `pinch` | 立即停止 |
| `stand`, `stand_up`, `standup`, `up` | `stand_up` | 站起 |
| `lay_down`, `laydown`, `lie_down`, `liedown`, `down`, `stand_down`, `standdown` | `stand_down` | 趴下 |
| `cw`, `clockwise`, `spin_clockwise` | `spin_clockwise` | 顺时针转向；移动中会变成弧线运动 |
| `ccw`, `counterclockwise`, `spin_counterclockwise` | `spin_counterclockwise` | 逆时针转向；移动中会变成弧线运动 |

## 运动逻辑

移动手势会生成一个有时间长度的运动片段。一个移动手势默认持续 `segment_duration_ms`，之后停止或切换到等待中的下一个指令。

相互冲突的移动指令不会立刻覆盖当前指令。例如先输入 `forward`，再输入 `left`，节点会先继续完成当前前进片段，把 `left` 保存为等待中的平移指令，然后短暂停顿，再开始向左平移。

系统只保存一个等待中的平移指令。如果等待期间又来了新的冲突平移指令，新的会覆盖旧的。

重复同方向移动会刷新当前运动片段的持续时间。

转向手势会修改当前运动。如果正在移动，`cw` 和 `ccw` 会增加 yaw，让机器人走弧线。如果当前没有移动，则会原地转向。

`pinch` 或 `stop` 会立即清除当前运动和等待中的运动。

如果移动过程中收到 `stand_down`，当前运动片段会先结束，然后节点停止/短暂停顿，再发送 `StandDown`。stand-down 等待期间，后续移动和转向手势会被忽略。

当机器人处于趴下或正在趴下状态时，移动和转向手势会被忽略。趴下后可以用 `stand` / `stand_up` 站起。

## 编译

先把 `UNITREE_ROS2_DIR` 设置为你的 `unitree_ros2` 仓库根目录：

```bash
export UNITREE_ROS2_DIR=~/ros2_ws/unitree_ros2
```

在 example 工作区中执行：

```bash
cd "$UNITREE_ROS2_DIR/example"
source "$UNITREE_ROS2_DIR/setup.sh"
colcon build --packages-select ring_control
source install/setup.bash
```

如果只是编译，不连接真实机器人，可以直接 source ROS 和 Unitree 消息工作区：

```bash
cd "$UNITREE_ROS2_DIR/example"
source /opt/ros/humble/setup.bash
source "$UNITREE_ROS2_DIR/cyclonedds_ws/install/setup.bash"
colcon build --packages-select ring_control
source install/setup.bash
```

## 检查机器人连接

发送真实指令前先检查连接：

```bash
cd "$UNITREE_ROS2_DIR"
source ./setup.sh
ros2 topic list
ros2 topic echo --once /sportmodestate
```

应该能看到：

- `/api/sport/request`
- `/api/sport/response`
- `/sportmodestate`
- `/lf/sportmodestate`

如果看不到这些 topic，不要进行真实控制测试。

## 安全干跑测试

可以通过 topic remap 测试完整链路，但不向机器人真实控制 topic 发布。

终端 1：

```bash
cd "$UNITREE_ROS2_DIR"
source ./setup.sh
source ./example/install/setup.bash
ros2 run ring_control ring_control_node --ros-args -r /api/sport/request:=/ring/test_api_sport_request
```

终端 2：

```bash
cd "$UNITREE_ROS2_DIR"
source ./setup.sh
source ./example/install/setup.bash
ros2 run ring_control ring_gesture_bridge_node
```

终端 3：

```bash
cd "$UNITREE_ROS2_DIR"
source ./setup.sh
source ./example/install/setup.bash
ros2 run ring_control ring_terminal_input_node
```

可选监控：

```bash
source "$UNITREE_ROS2_DIR/setup.sh"
source "$UNITREE_ROS2_DIR/example/install/setup.bash"
ros2 topic echo /ring/test_api_sport_request
```

## 在真实机器人上运行

需要三个终端。

终端 1：

```bash
cd "$UNITREE_ROS2_DIR"
source ./setup.sh
source ./example/install/setup.bash
ros2 run ring_control ring_control_node
```

终端 2：

```bash
cd "$UNITREE_ROS2_DIR"
source ./setup.sh
source ./example/install/setup.bash
ros2 run ring_control ring_gesture_bridge_node
```

终端 3：

```bash
cd "$UNITREE_ROS2_DIR"
source ./setup.sh
source ./example/install/setup.bash
ros2 run ring_control ring_terminal_input_node
```

建议先使用保守测试序列：

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

测试时保持周围空旷，并准备好手动停止方式。不要同时运行 `custom_walk_node` 和 `ring_control_node`，因为它们都可能向 sport control topic 发布指令。

## 参数

| 参数 | 默认值 | 含义 |
| --- | ---: | --- |
| `gesture_topic` | `/ring/gesture` | 标准手势输入 topic |
| `forward_speed` | `0.25` | 前进/后退速度幅值 |
| `left_speed` | `0.32` | 左平移速度 |
| `right_speed` | `0.27` | 右平移速度 |
| `turn_speed` | `0.7` | 原地转向速度 |
| `turn_step` | `0.2` | 移动中每次转向手势增加的 yaw |
| `max_turn_speed` | `1.0` | 最大 yaw 速度 |
| `period_ms` | `100` | 运动 heartbeat 发布周期 |
| `gesture_timeout_ms` | `2000` | 距离上次有效手势的安全超时 |
| `segment_duration_ms` | `1500` | 一个移动手势的持续时间 |
| `settle_duration_ms` | `250` | 切换冲突平移方向前的短暂停顿 |
| `motion_cooldown_ms` | `100` | 过滤戒指噪声输入的短冷却时间 |
| `posture_transition_ms` | `2500` | 本地估计的站起/趴下完成时间 |

常用调参：

- 单次移动太短：增加 `segment_duration_ms`。
- 左右平移不均衡：调整 `left_speed` 和 `right_speed`。
- 前进/后退太慢：调整 `forward_speed`。
- 转向太弱或太强：调整 `turn_speed` 或 `turn_step`。
- `gesture_timeout_ms` 是安全超时，不建议当作普通动作持续时间来调。

## 排查问题

看不到机器人 topic：

- 检查网络连接。
- source `$UNITREE_ROS2_DIR/setup.sh`。
- 确认 `ros2 topic echo --once /sportmodestate` 有数据。

终端测试器提示输入不支持：

- 输入 `help`。
- 使用支持输入表中的别名。

机器人移动时间太短：

- 增加 `segment_duration_ms`。

左/右平移较弱：

- 调整 `left_speed` 或 `right_speed`。

机器人忽略移动：

- 检查是否有 pending 的 `stand_down`。
- 检查机器人是否处于趴下、正在站起或正在趴下状态。
- 查看 `ring_control_node` 的日志。
