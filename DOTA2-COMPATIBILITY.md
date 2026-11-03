# Dota 2 候选窗口兼容

此修改迁入此前验证过的 Dota 原生定位及兼容小窗防闪烁逻辑。帝国时代 2 使用独立的位置适配器，说明见 [AOE2-COMPATIBILITY.md](AOE2-COMPATIBILITY.md)。两者共用候选窗口与按命名规则启用的系统小窗抑制，不包含位置诊断探针，也不修改 Ctrl+Shift 切换行为或服务连接时机。

## 启用

构建并安装包含此修改的 x64 TSF DLL 后，将根目录 `game_compat.ini.example` 的 `[dota2.exe]` 段合并到 Rime 用户目录的 `game_compat.ini`。用户目录取注册表 `HKCU\Software\Rime\Weasel` 的 `RimeUserDir`，未设置时使用 `%APPDATA%\Rime`。

规则按进程名匹配，未配置的程序保持原有候选行为。修改规则后，在下一次组合输入时重新加载；激活阶段的小窗观察器在新建/重新激活 TSF 实例时也会读取规则。停用可将 `enabled` 改为 `0`；要确保已加载的显示前拦截也退出，可重开游戏。

## 行为

- `force_own_ui=1`：即使游戏请求自行绘制候选，也允许小狼毫显示自身候选窗口。
- `position=native`：从 `IMEManager001` 找到当前聚焦输入控件，读取其原生窗口内几何位置。使用输入框左边缘和上下边界放置候选，而非逐字移动的插入光标。候选更新时查询，组合期间每 125ms 刷新；取消、提交、失焦和停用时隐藏并停止刷新。
- `hide_system_ime_ui=1`：仅在 Dota 进程和当前 GUI 线程观察兼容窗口创建，为 `CiceroUIWndFrame` 安装子类回调。在 `WM_WINDOWPOSCHANGING` 阶段验证所有者、进程、线程及小窗尺寸/位置，清除 `SWP_SHOWWINDOW`，避免先显示再隐藏造成闪烁。停用时清理观察器和子类，不销毁系统窗口。
- `debug=0`：默认关闭高频文件日志。

不使用 OCR、独立识别进程、鼠标位置或固定聊天框坐标。Dota 适配不再依赖具体 PE 时间戳、映像大小白名单或固定函数/对象 RVA。通过导出的 `CreateInterface("IMEManager001")` 获取现有管理器，验证模块 PE 节区、只读虚表、MSVC RTTI 对象类型和接口方法实现特征，再取得活动视图、聚焦文本控件及其面板。面板指针、宽高字段偏移从当前版本的简单 getter 实现中解析，控件基址从 RTTI 的接口偏移还原，输入框位置由控件现有的虚方法计算。查询前后复核活动视图和聚焦控件，并检查结果是否为游戏窗口内的有效矩形。

普通更新仅移动代码/数据地址、改变构建标识或这些 getter 字段偏移时，无需新增版本白名单；Dota 适配运行时也不扫描整个模块。虚表槽位和方法实现特征仍是对现有接口 ABI 的约束，接口布局、类型或实现形态改变时会拒绝读取并隐藏候选，需要重新调查，不承诺兼容任意未来更新。帝国时代 2 使用独立的 Widget 特征解析器，见 [AOE2-COMPATIBILITY.md](AOE2-COMPATIBILITY.md)。所有显示入口（包括 TSF 焦点通知）都重新验证原生位置；组合结束或失焦后禁止再次显示旧候选。TSF 界面注册失败时不启动位置刷新，结束时仅注销成功注册的界面，并保证释放自身窗口。

## 验证

在 x86 或 x64 Visual Studio Developer Command Prompt 中运行 `test\TestGameCompatibility\test.bat`，检查命名规则、坐标边界及真实 Win32 测试窗口的显示前拦截。新增 Dota 测试使用三份合成模块和 getter，验证模块基址/RVA/时间戳、接口调整量、面板与尺寸字段变化后仍能得到同一输入框位置，并覆盖失焦、查询期间切换焦点、错误类型、RTTI、方法特征及可写虚表拒绝。测试不会启动游戏、发送聊天或修改已安装的输入法。运行产物分别放在被 Git 忽略的 `msbuild\game-compat-tests\Win32` 和 `msbuild\game-compat-tests\x64`。游戏原生位置适配仅支持 x64 接口；x86 测试验证共用界面逻辑和安全跳过 x64 适配。

先构建本仓库的 `WeaselTSF` Release x64，再运行以下生命周期回归测试。它链接实际 TSF 编译对象，以模拟的 TSF 管理器验证无焦点文档、界面注册失败、重复结束、文档焦点变化、位置定时器清理和接口失效后的重启；不注册输入法、不连接算法服务、不启动游戏。

```bat
msbuild test\TestGameCompatibility\lifecycle-tests.vcxproj /p:Configuration=Release /p:Platform=x64
msbuild\game-compat-tests\x64\lifecycle-tests.exe
```

验证 x86 时先构建 `WeaselTSF` Release Win32，再将上面的 `/p:Platform=x64` 改为 `/p:Platform=Win32`，运行 `msbuild\game-compat-tests\Win32\lifecycle-tests.exe`。两种架构共用候选界面生命周期代码，测试链接各自的编译对象。

游戏实测需另行验证搜索框、大厅/对局聊天、输入框移动、Esc 取消、选词提交、切换前台程序及 Ctrl+Shift 切离/切回。源码迁移和隔离测试不代表已完成新构建的游戏实测。

本次已在更新后的 Dota `client.dll`（时间戳 `6ae937ac`、映像大小 `6e3b000`）实测，日志确认原生位置有效、候选显示和停止刷新；用户确认“候选位置正确，提交和取消正常”。这些构建标识仅记录测试环境，不参与运行时放行。两种架构编译、DLL 加载和隔离回归测试通过；不代表已覆盖所有游戏控件、未来接口变化或长时间游玩。
