# hypr-edgehover — fork Hyprland 0.57.0 只读审查

已完成源码审查、隔离构建和现有测试。没有发现 **blocker** 级发布阻塞项。发现 **5 项 should-fix、2 项 nit**。建议发布前修复输入行为问题。编译通过不代表运行验收通过。

本报告检查当前发布行为，不主张全部问题由本次移植引入。各项结论均重新对照本次插件和 fork 源码。

## 基线与范围

- 插件：`b717823be6da6799ec89c43e4eb62cd146935271`。本地 `main`、`origin/main` 和 `origin/HEAD -> origin/main` 指向该提交。没有访问远端刷新引用。
- Hyprland：`f6725e43eed50eaa061aa25fe7c9f8cdc9d355e2`，不是 upstream `0aa6745f`。
- aquamarine：`4abfcabbf3908cabccead39e5b5b8361dbf2546a`；隔离 pkg-config 报告版本 `0.15.1`。
- hyprutils：`v0.14.3`；hyprland-protocols：隔离 pkg-config 报告版本 `0.8.0`。
- `$S`：`/tmp/claude-1000/-home-wilf-data-hyprland-plugins/a85e6616-cf82-4173-94c4-fde1f868f04e/scratchpad`。
- 下文 `Hyprland/...` 指 `$S/hl057/Hyprland/...`。其他源码路径相对本报告目录。
- 独立构建副本 `$R`：`/tmp/edgehover-review-057.4vZx99`，由 `git archive HEAD` 创建。构建、日志和反例均留在该目录。
- 当前插件目录仅新增本报告。没有编辑插件源码，没有提交、推送、运行 hyprpm 或操作合成器。

## 发现

### should-fix 1 — 未排除带 `confine_pointer` 的目标，合成悬停会触发真实光标移动

- **插件证据**：`src/edgehover.cpp:181-183` 定义规则查询；`src/edgehover.cpp:344-360` 只检查当前焦点窗口；`src/edgehover.cpp:572` 执行该检查。候选过滤 `src/edgehover.cpp:658-672` 没有排除带该规则的目标。`src/edgehover.cpp:500-506` 先设置指针焦点，再调用 `rawWindowFocus`。
- **fork 证据**：`Hyprland/src/managers/input/InputManager.cpp:1910-1913` 明确限定 `isConstrained()` 只报告原生约束。`Hyprland/src/desktop/state/FocusState.cpp:127` 创建约束暂停作用域。`Hyprland/src/managers/input/PointerConstraintController.cpp:78-99` 根据指针与键盘焦点激活目标规则；`:112-127` 在恢复时执行 `enforce()`；`:195-205` 计算规则区域内最近点；`:226-227` 用 `warpTo` 移动真实光标。
- **失败场景**：当前窗口 A 没有约束。边缘目标 B 带 `confine_pointer`。默认 `follow_mouse=1` 时，插件允许选择 B，并把指针与键盘焦点切换到 B。作用域退出时，fork 把仍在外侧间隙中的真实光标拉入 B。该行为违背 README 所述的“保持真实光标位置”。即使 B 尚未获得键盘焦点，当前窗口检查也无法提前排除 B。
- **建议**：候选选择和固定拖动目标的交付检查都排除 `confinePointer(window)`。原生约束和规则约束分别检查。补充“当前窗口无约束、目标窗口有约束”的回归用例。
- **验证**：已验证源码调用链。未验证真实 Wayland 约束会话和屏幕上的光标移动。

### should-fix 2 — 鼠标移动监听器会取消触摸命中，导致边缘触摸没有接收对象

- **插件证据**：`src/edgehover.cpp:388-390` 监听通用 `input.mouse.move`。`src/edgehover.cpp:515-585` 没有区分触摸触发的 refocus。`src/edgehover.cpp:693-699` 成功交付指针移动后取消事件，却不填写触摸使用的 `m_found*ToFocus`。
- **fork 证据**：`Hyprland/src/managers/input/Touch.cpp:17-18` 标记触摸输入；`:58-62` 用触摸坐标调用 `refocus`。`Hyprland/src/managers/input/InputManager.cpp:1842-1843` 将其转入 `mouseMoveUnified`；`:279-281` 清空命中结果；`:290-294` 在重新命中前发出插件监听的事件，并在取消时返回。命中结果实际在 `:616-620` 填写。`Hyprland/src/managers/input/Touch.cpp:119-122` 读取这些结果；`:149-152` 在没有对象时提前返回，跳过 `sendTouchDown`。
- **失败场景**：关闭触摸工作区滑动，触摸设备在启用的 LAYER 窃取区点击非独占键盘焦点的面板。插件找到相邻窗口，发送合成指针移动并取消事件。触摸路径随后读取到空命中结果，面板和目标窗口均收不到这次 touch down。纯间隙和 OVERHANG 命中也经过同一取消路径。
- **建议**：让监听器只截获符合策略的指针输入。触摸 refocus 必须保留 fork 的触摸命中流程。优先使用明确的输入来源或调用上下文；不得直接把一个跨事件保留的“最后输入是触摸”标记当成完整区分方案。
- **验证**：已验证源码控制流。未执行触摸设备或嵌套触摸注入。

### should-fix 3 — 拖动仅固定窗口，没有固定按下时的表面

- **插件证据**：`src/edgehover.hpp:48-52` 的合成状态只保留窗口。`src/edgehover.cpp:734-743` 在按下时仅固定该窗口。`src/edgehover.cpp:587-606` 的后续拖动仍调用 `deliverSyntheticMotion`；`:485` 重新命中窗口内部表面；`:502` 重新设置指针焦点。
- **fork 证据**：`Hyprland/src/desktop/state/ViewHitTester.cpp:269-287` 会返回弹出表面或子表面。`Hyprland/src/managers/SeatManager.cpp:349-350` 只在表面相同时保持焦点；`:373-377` 在切换表面时发送 leave。`Hyprland/src/protocols/core/Seat.cpp:192-200` 在 leave 时释放已按下的按钮。fork 原生拖动路径在 `Hyprland/src/managers/input/InputManager.cpp:391-395` 保持原表面。
- **失败场景**：边缘合成点击落在一个窗口的根表面。持续按住按钮并沿边缘拖动，虚拟命中点进入该窗口的另一子表面。窗口引用没有变化，插件仍标记为 sticky，但 fork 已向旧表面发送按钮 release。客户端拖动会提前结束。
- **建议**：按下时固定实际接收按钮的 `CWLSurfaceResource`。拖动期间只更新该表面的局部坐标，不重新选择同窗口的另一表面。销毁或解除映射时清除固定状态。
- **验证**：已验证离线反例，原样插件方法在物理释放前把指针切换到子表面；同表面移动的对照保持焦点。自动 release 的结论来自上述 fork 源码。未验证真实客户端拖动。

### should-fix 4 — 相同几何评分会优先选中底层重叠窗口

- **插件证据**：`src/edgehover.cpp:658-672` 按 `viewState()->windows()` 正序生成候选，索引随顺序增加。`src/geometry.cpp:238-253` 在距离相同时选择较小索引。交付路径 `src/edgehover.cpp:681-699` 没有检查虚拟目标点是否由更高窗口遮挡。
- **fork 证据**：`Hyprland/src/desktop/state/ViewState.cpp:8-9` 返回原窗口序列。`Hyprland/src/desktop/state/WindowState.cpp:59` 把序列尾部视为顶部；`:99-107` 将抬高窗口移至末尾。`Hyprland/src/desktop/state/ViewHitTester.cpp:82-83` 倒序命中浮动窗口；`:136-138` 明确说明窗口序列按 bottom-to-top 排列。
- **失败场景**：两个浮动窗口位于同一显示器和工作区，位置与大小完全相同，左侧留有间隙。光标位于左侧边缘间隙。两窗口评分相同，插件把悬停送到完全遮挡的底层窗口；默认配置还允许向该窗口送点击。
- **建议**：至少按顶部优先处理相同评分。完整修复必须在虚拟目标点核对遮挡、浮动层和固定窗口的优先级，不能仅凭一个全局索引解决所有层次情况。
- **验证**：已验证离线反例选中底层候选。移除底层候选后，对照选中唯一的顶部候选。未验证屏幕上的重叠窗口。

### should-fix 5 — Viewflow 检查未覆盖 fork 的原生输入捕获

- **插件证据**：`src/edgehover.cpp:9-19` 仅查询名为 `viewflow-hyprland` 的插件。移动、按钮和滚轮入口分别在 `:518`、`:705`、`:775` 使用该查询。滚轮禁止转发时，`:791-794` 会主动调用 `simulateMouseMovement()`；没有原生捕获检查。
- **fork 证据**：`Hyprland/src/protocols/InputCapture.cpp:366-367` 用独立的 `active` 状态表示原生捕获。`Hyprland/src/managers/input/InputManager.cpp:160-163` 在原生捕获时跳过普通相对移动。按钮监听先于原生捕获处理，见 `:720-740`；滚轮监听先于原生捕获处理，见 `:985-1006`。`simulateMouseMovement` 在 `:195-197` 直接调用 `mouseMoveUnified`，没有经过相对移动的捕获门禁。
- **失败场景**：本地 OVERHANG 悬停使合成状态生效，随后原生捕获启动。普通相对移动不再调用插件，因此旧合成状态仍保留。默认 OVERHANG 禁止滚轮转发。下一次滚轮事件进入插件后触发 `simulateMouseMovement`，在原生捕获处理滚轮之前执行本地命中与指针交付。原本会在 `passEvent=false` 时返回的滚轮路径由此产生本地输入副作用。原生捕获期间，按钮入口也会根据旧合成状态建立 sticky 状态。
- **建议**：为三个入口增加共同的原生捕获检查。捕获时清除合成、sticky 和绕过状态，不触发本地模拟移动。按钮和滚轮必须保留给 fork 的原生捕获路径，不得照搬 Viewflow 分支的 `info.cancelled=true`。
- **验证**：已验证事件顺序和源码调用链。未验证 portal/libeis 会话，也未验证 Viewflow 与原生捕获同时活动。

### nit 1 — 合成悬停跳过边框光标覆盖的清理

- **插件证据**：`src/edgehover.cpp:500-506` 更新焦点相关字段，但不清理边框光标覆盖。`src/edgehover.cpp:698-699` 随后取消 stock 路径。
- **fork 证据**：`Hyprland/src/managers/input/InputManager.cpp:293-294` 在取消时返回。正常清理同时重置 `m_borderIconDirection` 和 `CURSOR_OVERRIDE_WINDOW_EDGE`，见 `:641-647`、`:699-701`。`:2290-2304` 用方向缓存控制覆盖更新。`Hyprland/src/pointer/cursor/CursorShapeOverrideController.cpp:17-23` 负责撤销覆盖。
- **失败场景**：启用边框调整大小光标。光标从窗口边框进入有效边缘间隙。插件持续取消普通移动路径，上一处边框的 resize 光标覆盖不会沿正常路径清除。
- **建议**：开始接管合成悬停时，同时清理边框方向缓存和对应覆盖组。不要只修改其中一项。
- **验证**：已验证源码清理路径缺失。未验证屏幕上的光标形状。

### nit 2 — 无条件 GCC 参数使 Clang 构建失败

- **插件证据**：`CMakeLists.txt:11-21` 对所有编译器和目标添加 `--no-gnu-unique`。
- **fork 对照**：`Hyprland/CMakeLists.txt:101-118` 定义 fork 的语言标准与常用编译参数，没有要求插件使用该 GCC 专用参数。fork 的正常卸载边界是 `Hyprland/src/plugins/PluginSystem.cpp:143-184`。关闭 GNU unique 是插件的卸载策略，不是该 fork 的通用编译器参数要求。
- **失败场景**：使用本机 Clang 23.1.3 配置本插件。连不依赖 Hyprland 的 geometry test 都无法编译，错误为 `clang++: error: unknown argument: '--no-gnu-unique'`。本次 GCC 基线构建不受影响。
- **建议**：按编译器或参数能力添加该选项；GCC 可以使用受支持的 `-fno-gnu-unique`。对最终共享库检查 GNU_UNIQUE 符号。不要无条件把选项改成另一个 Clang 也不支持的写法。
- **验证**：已验证 Clang 配置通过、geometry target 构建失败。未执行删除该参数后的完整 Clang 插件构建。

## 接口、ABI 和生命周期核对

下表是源码核对范围。除上述发现外，没有在这些调用中发现本基线的接口不匹配。源码核对不等于合成器运行验证。

| 范围 | 插件位置 | fork 对照与结论 |
|---|---|---|
| ABI 门禁 | `src/main.cpp:39-59` | `Hyprland/src/plugins/PluginAPI.cpp:23-35`、`Hyprland/src/plugins/PluginAPI.hpp:394-405`。本地拼接提交号及依赖的 major/minor，与 fork 公式一致。比较在配置注册和监听器创建之前执行；不匹配时抛异常。`:59` 之前只有句柄赋值。fork 在 `Hyprland/src/plugins/PluginSystem.cpp:109-121` 捕获初始化异常并卸载。未动态测试匹配/不匹配加载。 |
| 配置注册、读取与重载 | `src/main.cpp:17-36,63-88`；`src/edgehover.cpp:153,162,404-442` | `Hyprland/src/plugins/PluginAPI.cpp:375-391`、`:109-111`；`Hyprland/src/config/lua/ConfigManager.cpp:1125-1141`；`Hyprland/src/config/ConfigValue.hpp:74-93`。V2 的整数/字符串值与读取类型一致。内置 `special_fallthrough`、`modal_parent_blocking`、`follow_mouse` 仍使用配置值接口。通知签名见 `Hyprland/src/plugins/PluginAPI.cpp:114-122`。 |
| 三个可取消事件 | `src/edgehover.cpp:387-398` | `Hyprland/src/event/EventBus.hpp:41-43,55-59,127-131`；发出点为 `Hyprland/src/managers/input/InputManager.cpp:292,722,986`。参数类型直接编译核对。move 在 stock 命中之前；button 在 held-button 记账之前；axis 在原生捕获之前。触摸复用与捕获门禁见 should-fix 2、5。 |
| 显示器、工作区和全屏 | `src/edgehover.cpp:134-158,547-560,582` | `Hyprland/src/state/MonitorQuery.cpp:39-42,64-100`；`Hyprland/src/output/Monitor.cpp:670` 的 `m_size` 是缩放、变换后的逻辑尺寸；`m_position` 与鼠标坐标同为逻辑坐标。`Hyprland/src/managers/fullscreen/FullscreenController.cpp:107-137,211-219`：显示器级查询仅 full fullscreen，插件又查询选中工作区，覆盖 maximized 情况。 |
| 窗口与候选字段 | `src/edgehover.cpp:70-76,149-183,658-672` | `Hyprland/src/desktop/view/window/Window.hpp:152,176-183,211,235,257-259`；`Hyprland/src/desktop/view/types/GeometricAnimated.cpp:8-29`。`m_monitor` 是弱引用；`m_workspace` 是工作区引用。mapped/hidden/input/focus、modal、noFocus、noFollowMouse 过滤仍有对应语义。目标使用 goal 几何；最终包围框使用 current 几何。这两类几何没有混作同一字段。排序和目标规则缺口见 should-fix 1、4。 |
| 窗口表面与 X11 | `src/edgehover.cpp:468-495` | `Hyprland/src/desktop/state/ViewHitTester.cpp:269-290` 的 Wayland 命中以 goal 位置为基准；未命中时插件退回 current 位置的根表面路径，与 fork 的 `Hyprland/src/managers/input/InputManager.cpp:534-543` 对应。X11 的 `surfaceLocalToBuffer` 在 `Hyprland/src/desktop/view/window/X11Backend.cpp:326-327` 乘表面缩放。离线原有测试覆盖 X11 坐标转换；动画和真实分数缩放未验证。 |
| layer 命中与命名空间 | `src/edgehover.cpp:276-330,610-649` | `Hyprland/src/desktop/state/ViewHitTester.cpp:322-359`；`Hyprland/src/desktop/view/LayerSurface.hpp:83`。插件使用 layer popup、overlay/top、window、bottom/background 的分类顺序，并读取 `m_namespace`。真实 popup 与 layer 输入区域未运行验证。 |
| 当前焦点与强制焦点 | `src/edgehover.cpp:333-375,505-506` | `Hyprland/src/desktop/state/ViewQuery.cpp:242-255`；`Hyprland/src/desktop/state/FocusState.hpp:52-53`；`Hyprland/src/desktop/state/FocusState.cpp:98-225`。保留 `rawWindowFocus`，没有替换成带额外策略的高层 focus。`CWLSurface::fromResource` / `CWindow::fromView` 用于当前焦点归属；规则约束的恢复副作用见 should-fix 1。 |
| 输入与安全退出条件 | `src/edgehover.cpp:563-585` | `Hyprland/src/managers/input/InputManager.cpp:1910-1913,1924`；`:391-425` 的 held-surface 路径；`Hyprland/src/managers/SessionLockManager.hpp:61`。插件读取 held buttons、seat grab、layout drag target、exclusive keyboard layers、IME popup、forced focus 和 session lock。`m_emptyFocusCursorSet` / `m_lastFocusOnLS` 在交付时设为 false，参考 `Hyprland/src/managers/input/InputManager.cpp:234,697,709-710`。 |
| 指针交付与时间 | `src/edgehover.cpp:500-506` | `Hyprland/src/managers/SeatManager.cpp:346-436`；`Hyprland/src/helpers/time/Time.hpp:15,22`。使用单调时钟毫秒，交给接受 `uint32_t` 毫秒的 seat 接口。`setPointerFocus` 会发送 enter/leave，不能按无副作用赋值理解；见 should-fix 3。 |
| Viewflow 可选查询 | `src/edgehover.cpp:9-19` | `Hyprland/src/plugins/PluginSystem.hpp:16,25,45`、`Hyprland/src/plugins/PluginSystem.cpp:256`。每次查当前插件句柄并 `dlsym`，未跨事件保存函数地址。该外部导出函数的实际实现不在审查对象内，未验证其语义。 |
| 函数 hook | 全部 `src/` | 无 `findFunctionsByName`、`createFunctionHook` 或原函数转发。符号匹配、重载歧义、hook 签名和 hook `static_assert` 项均不适用。 |

### 卸载边界

- `src/main.cpp:99-100` 在 `PLUGIN_EXIT` 中销毁 `EdgeHover`。
- `src/edgehover.cpp:382-385` 先移除未执行的 Wayland idle source。idle 回调只捕获 `this`，见 `src/edgehover.cpp:751-759`。现有离线测试包含销毁后再分发 idle 的检查。
- 三个监听器由 `src/edgehover.hpp:92-94` 持有。对象销毁释放监听器。fork 通过 `Hyprland/src/helpers/signal/Signal.hpp:3-6` 使用 hyprutils 的 signal；隔离 `hyprutils/src/signal/Signal.cpp:21-35` 跳过失去外部持有者的监听器，`:51-58` 以弱引用保存监听器。
- 没有插件自有 timer、Lua 函数、dispatcher 或函数 hook。`reloadConfig()` 安排的回调属于 fork 的 `PluginAPI.cpp:109-111`，不捕获插件对象。
- 配置由 fork 按句柄清理：`Hyprland/src/plugins/PluginSystem.cpp:174-184` 在 `dlclose` 前调用 `onPluginUnload` 并删除插件对象；`Hyprland/src/config/lua/ConfigManager.cpp:1304-1314` 删除插件配置。`CConfigValueBase` 的静态读取对象在析构时从注册表移除，见 `Hyprland/src/config/ConfigValue.cpp:26-32`。
- 最终 GCC 共享库包含 **0 个 GNU_UNIQUE 符号**。没有找到正常 `PLUGIN_EXIT` 路径在 `dlclose` 后保留插件回调的源码证据。反复加载/卸载的真实运行行为未验证。

### 构建元数据

- `README.md:15` 明确标注 fork、`f6725e43` 和 Lua 配置。配置示例与 V2 注册路径一致。
- `CMakeLists.txt:28-41` 从选定 pkg-config 头文件树寻找 `src/plugins/PluginAPI.hpp`，使用 `NO_DEFAULT_PATH`。构建命令确认 `<src/...>` 使用隔离 prefix，没有使用 `/usr/include/hyprland`。
- `hyprpm.toml:8-11` 的产物 `build-cmake/libhypr-edgehover.so` 与正常 CMake 输出一致。审查 helper 使用 `build-057/` 是本次隔离目录选择，不是元数据矛盾。
- 本仓库没有 flake。CMake 没有把 fork 提交写死；运行时 ABI 门禁承担拒绝错误头文件构建的职责。没有执行 hyprpm 安装验证。

## 命令与结果

以下命令在插件目录执行，或在命令中显式指定目标。源码查阅另使用 `rg -n`、`rg --files`、`nl -ba` 和 `sed -n` 定位上述引用。

```sh
S=/tmp/claude-1000/-home-wilf-data-hyprland-plugins/a85e6616-cf82-4173-94c4-fde1f868f04e/scratchpad
R=/tmp/edgehover-review-057.4vZx99
git status --short
git log -4 --oneline
git rev-parse HEAD
git remote -v
git branch -av
git -C "$S/hl057/Hyprland" rev-parse HEAD
git -C "$S/hl057/aquamarine" rev-parse HEAD
git -C "$S/hl057/hyprutils" describe --tags --always
git archive HEAD | tar -x -C "$R"
bash "$S/hl057/build.sh" "$R"
tail -n 12 "$R/build-057.log"
source "$S/hl057/env.sh"
ctest --test-dir "$R/build-057" --output-on-failure
pkg-config --modversion hyprland aquamarine hyprutils hyprland-protocols
pkg-config --cflags hyprland
ninja -C "$R/build-057" -t commands
readelf -Ws "$R/build-057/libhypr-edgehover.so" | awk '$5 == "UNIQUE" {n++} END {print "GNU_UNIQUE symbols:",n+0}'
sha256sum "$R/build-057/libhypr-edgehover.so"
git diff --check
git diff --exit-code
git status --porcelain=v1
```

**已验证：GCC 构建与现有测试。**

- 编译器：GCC `16.2.1 20260810`。
- `build-057.log` 最后一行为 `EXIT 0`，完成共享库链接。
- CTest：`hypr-edgehover-geometry-test` 通过；`hypr-edgehover-input-regression` 通过。共 `2/2`，耗时 `4.13 s`。
- 输入回归测试从生产源码提取方法，但使用离线 host，见 `tools/input_regression.py:15-32`。它不加载合成器。其 hit tester、focus、seat 和部分规则函数是替身，不能据此认定实际输入行为通过。
- 共享库 SHA-256：`9c60c5cc4afcf10e5fc93f74ded9384921b31a7d01cba57da90ddf473abf6238`。
- 写报告前，`git diff --check`、`git diff --exit-code` 通过；`git status --porcelain=v1` 无输出。
- 写报告后的只读核对：16 个受版本控制的文件逐字节匹配 `HEAD`；`git status --short` 仅显示 `?? REVIEW_057.md`。报告结构检查确认每项发现均有插件引用、fork 引用、失败场景、建议和验证边界。

**已验证：两项离线反例及对照。**

```sh
python /tmp/edgehover-review-057.4vZx99/review_probe.py
```

结果：

```text
REVIEW overlap: bottom target selected despite identical top window
REVIEW overlap control: sole top candidate selected
REVIEW sticky: pointer surface changed before physical release; same-surface control retained focus
```

`$R/review_probe.py` 复用原有输入测试的提取方式。生产 `EdgeHover` 方法和 geometry 源码保持原样。审查替身仅增加同窗口第二表面，并按虚拟坐标切换命中表面。它检查焦点切换，不模拟 Wayland 自动 release；自动 release 的证据来自 fork 源码。产物为 `$R/review_probe.cpp`、`$R/review_probe`、`$R/review_probe.log`。原有回归用例也在该程序中通过。

**失败：Clang 构建。**

```sh
clang++ --version
clang++ --no-gnu-unique -x c++ -fsyntax-only /dev/null
source "$S/hl057/env.sh"
cmake -S "$R" -B "$R/build-clang" -G Ninja \
  -DCMAKE_CXX_COMPILER=clang++ -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON
cmake --build "$R/build-clang" --target hypr-edgehover-geometry-test
```

Clang `23.1.3` 配置通过，编译失败。日志 `$R/clang-057.log` 最后一行为 `EXIT 1`。关键输出：

```text
clang++: error: unknown argument: '--no-gnu-unique'
ninja: build stopped: subcommand failed.
```

## 未验证

- 未启动嵌套合成器。上述发现由源码和离线反例支持；本次没有必须依赖嵌套运行才能判定的加载问题。
- 未动态测试 ABI 匹配成功与 ABI 不匹配拒绝。门禁顺序和 hash 公式仅完成静态核对。
- 未运行真实加载、列表查询、卸载或反复加载测试。未向宿主会话加载插件。
- 未验证真实触摸、子表面拖动、重叠窗口、规则约束、原生输入捕获、Viewflow、layer popup、X11 和分数缩放会话。
- 未做屏幕录制、视觉检查或真实硬件输入验收。
- 未执行 hyprpm、Nix 或安装包发布流程。
- 未修改或修复任何发现。独立副本中的测试和日志保留供复查。

## Fixes

已完成原报告的 5 项 should-fix 和 2 项 nit。另修复必需嵌套检查发现的 full-reset 配置丢失。没有未处理的原审查条目。以下记录更新上文的验证状态；原审查内容保留为基线证据。

### 修复范围与提交

- 修复分支：`fix/review-057`。原审查工作树和原分支的源码保持不变。
- 修复工作树 `$F`：`/tmp/claude-1000/-home-wilf-data-hyprland-plugins/a85e6616-cf82-4173-94c4-fde1f868f04e/scratchpad/hl057/review/hypr-edgehover-fix`。
- 测试证据目录 `$T`：`/tmp/edgehover-fix-tests`。本节插件源码位置相对 `$F`。
- 六个本地修复提交均使用祈使句标题，正文说明 fork 行为，末尾为 `Co-Authored-By: Codex <noreply@openai.com>`。没有推送。

| 原条目 | 提交 | 改动 | 回归测试与反向检查 | 未做 |
|---|---|---|---|---|
| should-fix 1：目标规则约束 | `d964b76` | `src/edgehover.cpp:494,711` 在候选和最终交付中排除 `confine_pointer`。拖动固定目标也经过最终检查。 | `confinement` 检查候选跳过、交付时规则变化和普通目标对照。通过。以基线源码反向运行时，选择断言失败；见 `$T/reverse-confinement.log`。 | 真实约束会话未验证。 |
| should-fix 2：触摸 refocus | `d964b76` | `src/edgehover.cpp:568-574` 同时检查上次触摸标记与本次 `mouseMoveUnified` 的 mouse 参数记录。触摸保留 stock 命中流程；第一个真实鼠标事件不受旧触摸标记阻挡。 | `touch` 检查触摸不取消、不交付合成移动；旧触摸标记下的首个鼠标事件仍交付。通过。基线源码触发触摸断言；见 `$T/reverse-touch.log`。 | 真实触摸设备未验证。 |
| should-fix 3：固定实际表面 | `da4665e` | `src/edgehover.hpp:52-53` 保存弱表面引用；`src/edgehover.cpp:514-522` 保持按下表面，并计算该表面的局部坐标；`:781-785` 验证按下时的 seat 焦点。表面或窗口失效时放弃合成拖动。 | `sticky` 检查根表面到子表面、子表面到根表面、局部坐标和 unmap。通过。替换为前一提交的交付逻辑后，固定表面的断言失败；见 `$T/reverse-sticky.log`。 | 真实客户端拖动未验证。 |
| should-fix 4：窗口遮挡 | `cf96d72` | `src/edgehover.cpp:709` 按顶部优先生成候选；`:503-509` 用 fork 命中器检查最终虚拟点。遇到另一个 stock 命中窗口时，保守返回普通路径，不穿过遮挡交付。按钮固定表面不重新命中。 | `overlap` 检查相同评分选择顶部与唯一候选对照；`occlusion` 检查 stock 优先级拒绝遮挡目标。通过。前一提交在两项测试中分别触发断言；见 `$T/reverse-overlap.log`、`$T/reverse-occlusion.log`。 | 浮动、固定窗口的屏幕行为未验证。 |
| should-fix 5：原生捕获 | `d964b76` | `src/edgehover.cpp:468-485` 统一清除合成、sticky、绕过标记和 idle。三个入口调用该检查。原生捕获保留事件给 fork；Viewflow 分支保留其取消语义。`CMakeLists.txt:33` 声明 `InputCapture.hpp` 所需的 `libeis-1.0` 头文件依赖。 | `capture` 检查三个入口、idle 移除、零本地模拟、捕获结束恢复及 Viewflow 对照。通过。基线源码触发状态清理断言；见 `$T/reverse-capture.log`。 | portal/libeis 和 Viewflow 的真实捕获会话未验证。 |
| nit 1：边框光标 | `d4b4ba3` | `src/edgehover.cpp:546-547` 同时清除方向缓存和窗口边缘覆盖组。 | `cursor` 检查两个状态均清除，其他覆盖组保留。通过。使用前一提交的交付逻辑时断言失败，程序退出 86；见 `$T/reverse-cursor.log`。 | 视觉验收未验证。 |
| nit 2：Clang 参数 | `3ef6454` | `CMakeLists.txt:22-25` 仅对 GCC 添加 `-fno-gnu-unique`。 | GCC 与 Clang 完整构建通过；各自三项测试通过；两个最终共享库均为零 GNU_UNIQUE。反向证据为原审查副本的 Clang 构建：恢复原有无条件参数后报 `unknown argument: '--no-gnu-unique'`，见 `/tmp/edgehover-review-057.4vZx99/clang-057.log`。 | Clang 产物没有加载到 GCC 构建的合成器。 |

触摸来源和边框方向在该 fork 中没有公共访问器。`src/input_compat.hpp` 先包含依赖，再仅对 `InputManager.hpp` 使用受限的 access override。没有新增函数 hook，也没有猜测字段偏移。原有精确 ABI 门禁仍在任何注册之前执行。后续迁移必须继续核对这些私有字段。

输入回归测试使用原样生产方法和离线替身。表面 release、命中器和光标覆盖均有明确替身范围。这些测试不构成实际触摸、Wayland 协议或视觉验收。早期反向检查由断言触发 SIGABRT；后续测试安装断言退出处理器，以退出 86 代替 core dump。Python 驱动对两种失败均返回非零。

### 必需检查发现的附加问题：full-reset 后配置丢失

提交：`bf2ce63`，`Restore plugin options after full configuration resets`。

- **实际证据**：七项原审查修复后，h3 中加载插件成功。`reload full-reset` 返回 `ok`，插件仍在加载列表中，但 `getoption plugin:hypr_edgehover:enabled` 返回 `no such option`。当次检查没有发生崩溃。日志：`$F/build-057/nested-before-config-fix.log`。
- **fork 原因**：`Hyprland/src/ipc/s1/Commands.cpp:1355-1364` 更换配置管理器，并在 `preReload` 前刷新 `CConfigValue` 缓存。`Hyprland/src/config/lua/ConfigManager.cpp:412-417` 的新管理器仅创建内置值；插件值原由 `:1125-1141` 保存在旧管理器中。`preReload` 发出点为 `:610`。
- **改动**：`src/plugin_config.hpp:12-16` 保留不绑定旧管理器缓存的类型描述；`:29-40` 仅恢复缺失配置，并保留 owner handle。使用配置是否存在进行判断，可处理管理器地址复用。`:53-68` 从当前管理器读取值，不保留数据指针。`src/main.cpp:86-89` 注册恢复监听器，`:108-111` 在退出时移除监听器。普通 reload 不重复注册配置。
- **离线验证**：新增 `hypr-edgehover-config-regression`。检查整数和字符串、值更新、新管理器、同地址缺失配置、正常重载无重复注册、缺失值安全返回及卸载清理。通过。移除恢复调用后，恢复值断言失败；见 `$T/reverse-config.log`。恢复 descriptor 的缓存绑定后，零缓存绑定断言失败；见 `$T/reverse-config-bind.log`。
- **嵌套验证**：修复后，普通 reload、两次 full-reset、整数和字符串查询、卸载清理均通过。`configerrors` 为空。卸载后选项查询返回 `no such option`，合成器仍响应版本查询。
- **Lua 附加项**：本插件没有调用 `HyprlandAPI::addLuaFunction`，也没有持有 Lua registry 引用。brief 的 Lua 函数重注册与 `CLuaRef` 专项不适用。没有为此引入 Lua 函数或 Lua 引用。

### 最终构建与完整测试

在 `$F` 执行：

```sh
S=/tmp/claude-1000/-home-wilf-data-hyprland-plugins/a85e6616-cf82-4173-94c4-fde1f868f04e/scratchpad
source "$S/hl057/env.sh"
bash "$S/hl057/build.sh" "$PWD"
ctest --test-dir build-057 --output-on-failure
cmake -S . -B build-clang -G Ninja -DCMAKE_CXX_COMPILER=clang++ \
  -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON
cmake --build build-clang -j2
ctest --test-dir build-clang --output-on-failure
```

**已验证**：GCC 16.2.1 构建的 `build-057.log` 末行为 `EXIT 0`。GCC 完整测试 `3/3` 通过，最终耗时 `2.98 s`。Clang 23.1.3 完整构建通过，`clang-057.log` 末行为 `EXIT 0`；其完整测试 `3/3` 通过，最终耗时 `3.29 s`。测试清单：

1. `hypr-edgehover-geometry-test`。
2. `hypr-edgehover-input-regression`。
3. `hypr-edgehover-config-regression`。

最终 CTest 日志：`$F/build-057/ctest-final.log`、`$F/build-clang/ctest-final.log`。

最终共享库 SHA-256：

- GCC：`c238896e5ecd1bf9bdf67e79816fa96e271c86b5182c90297c2a1f8ca046ee80`。
- Clang：`47e14a9eadd74a200b9d11bf87eaf40e062096d76b1c328c32d50c26e8f25814`。

### h3 嵌套加载、完整重置和卸载

所有 `run.sh` 和 `hyprctl` 调用均在以下变量导出后执行。没有使用默认 `/run/user/1000/h`，也没有访问其他 agent 的运行目录。

```sh
export NESTED_XDG=/run/user/1000/h3
export NESTED_LOG="$PWD/nested.log"
export XDG_RUNTIME_DIR="$NESTED_XDG"
```

命令脚本：`$F/build-057/nested-smoke.sh`。它启动 `$S/hl057/nested/run.sh`，从 h3 的 socket 路径选取嵌套实例签名，并导出 `HYPRLAND_INSTANCE_SIGNATURE`。使用系统 `hyprctl` 连接该签名。检查内容：

1. 查询嵌套版本，确认提交为 `f6725e43eed50eaa061aa25fe7c9f8cdc9d355e2`。
2. 加载 GCC 共享库，确认 `load: ok` 和插件列表。
3. 查询 `enabled`，确认 `int: 1`。
4. 普通 reload，查询 `configerrors`。
5. full-reset，重新确认版本、`enabled` 和插件列表。
6. 第二次 full-reset，查询 `edges`，确认 `str: lrtb`。
7. 确认 `configerrors` 为空。
8. 卸载插件，确认 `unload: ok`、`no plugins loaded` 和配置删除。
9. 确认合成器仍响应版本查询。
10. trap 停止并等待本脚本启动的嵌套进程。

**已验证**：最终脚本退出 `0`。日志为 `$F/build-057/nested-final.log`；固定 stdout/stderr 日志为 `$F/nested.log`；运行日志副本为 `$F/build-057/nested-runtime-final.log`。结束后 h3 没有活动的 `.socket.sock`。没有运行宿主 hyprctl、hyprpm 或推送。

首次试运行使用不存在的 fork 内 hyprctl 路径，退出 `127`。当次 trap 停止了嵌套进程。随后改用环境中的系统 hyprctl。该试运行未作为插件验证通过证据；日志为 `$F/build-057/nested-first.log`。

### 交付与验证边界

- 原审查工作树仅更新本报告。修复源码和测试位于 `$F` 的 `fix/review-057`。
- `README.md` 补充触摸、约束目标、固定表面、遮挡回退、原生捕获、full-reset 和构建依赖说明。
- 测试副本、反向检查与日志保留。修复工作树顶层的 `build-057.log`、`clang-057.log`、`nested.log` 是未提交的证据文件。
- 原七项审查问题和附加配置问题均有修复，没有等待设计决定的条目。
- 未验证真实触摸设备、实际客户端拖动、portal/libeis 捕获、Viewflow 联动、屏幕上的遮挡与光标外观。
- 嵌套检查仅证明 GCC 产物在指定 fork 中加载、配置重置和卸载。它不代表真实输入或视觉验收。
