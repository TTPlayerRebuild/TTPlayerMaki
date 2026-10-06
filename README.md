# ttp_maki.dll

独立的 C++ MAKI 字节码解释器，供 `ttp_waskin.dll` 动态调用。不依赖 Wasabi、gen_ff、Winamp 进程或播放引擎。

## 目录与构建

- `src/vm.cpp`：二进制校验、变量、调用栈、事件和受限解释执行。
- `include/ttp_maki.h`：版本化 C ABI；调用方持有对象，VM 持有程序和变量。
- `cmake/`：与播放器两版共用的 x86 / XP 工具链和导入检查。

在本目录执行：

```powershell
cmake -S . -B build -A Win32
cmake --build build --config Release --target ttp_maki --parallel 4
```

输出为 `build/Release/ttp_maki.dll`。把它与 `ttp_waskin.dll` 一起放入播放器的 `AddIn` 目录。普通版和 XP／Win7 版使用同一份 DLL。首次配置需要下载锁定版本和 SHA-256 的 VC-LTL / YY-Thunks；离线构建可通过标准 FetchContent 源码目录参数指定缓存。

## 边界

- VM 不读取皮肤 ZIP、不解析 XML、不创建窗口、不打开文件或网络；原生方法由调用方按 GUID 和方法名显式注册。
- ABI 只传定长字段、借用字符串和不透明对象指针；不跨 DLL 释放 STL/CRT 内存。
- 创建时检查全部导入、指令、索引和跳转目标；不支持的格式、指令和接口直接失败。
- 每条导入分别保留类 GUID、方法名与参数数量；不同类的同名方法不会覆盖彼此的签名。
- `POP` / `SET` 按声明的 int、float、double、boolean 或 string 类型赋值；`SET` 返回转换后的值。比较按左操作数类型转换右侧，字符串比较区分大小写。超出可表示范围的数值转换会终止该实例，避免依赖未定义行为。
- 每次外层事件最多 100000 条指令，单次调用最多 20000 条；事件深度 32、调用深度 256、栈 4096 项、字符串 65536 字符。失败实例停止执行后续事件。
- VM 的事件预算使用 `TlsAlloc/TlsSetValue`，不使用编译器 `thread_local`；CRT 和系统 API 的旧系统适配由 VC-LTL / YY-Thunks 提供。每个实例只能由创建线程执行。
- 当前覆盖 HeadAMP 主布局三份 MAKI 程序所需子集及 `complete`。不是完整 MAKI 实现：对象动态创建/删除、变量别名、未实现算术指令、配置服务等仍会拒绝。
- `getRuntimeVersion` 等属于界面宿主策略，不在 VM 中伪造 Winamp 服务。

## 验证

C++ 测试保存在 `../rebuild/tests/waskin/maki_vm_tests.cpp`，不随本工程发布，Action 不编译或执行测试。覆盖 ABI 协商、动态加载、实际字节码运算、声明类型转换、比较与逻辑运算、类 GUID 对应的签名、原生回调重入、complete、跨线程调用、执行预算、无效跳转/索引/导入和所有截断前缀。

类型处理对照本地 Winamp 源码 `Src/Wasabi/api/script/vcpu.cpp` 的 `VCPUassign`、`runCode` 及 `scriptmgr.cpp` 的 `assign`、`makeInt/Float/Double/Boolean`、比较函数；这是独立实现，不调用 Wasabi 服务。

本地构建通过 XP / Win7 静态导入检查。真实旧系统运行仍需在对应系统验证。

API 的规范头文件在本目录；`../waskin/include/ttp_maki.h` 是调用方 SDK 副本，修改 ABI 时应同步更新。

## 日期版本与 Release 体积优先构建

DLL 的文件版本和产品版本使用北京时间 `yyyy.MM.dd`，同日发布补丁使用 `pN`；
例如 `2026.10.06p1` 对应固定数字版本 `2026.10.6.1`。Actions 在编译前确定最终版本，
DLL、发行包和发布标签使用同一版本。各项目继续独立构建。

Release 的统一配置见 [cmake/size_release.cmake](cmake/size_release.cmake)：
`/O1 /Os /Gy /Gw /GF`、跨模块优化和链接去除未引用代码／折叠相同代码，关闭 Release 调试信息。
本项目经 `/Ob0`、`/Ob1`、`/Ob2` 对比，默认选择 `/Ob2`；
可用 `-DTTP_SIZE_INLINE_LEVEL=0|1|2` 重新测量不同内联策略。
保留正常浮点语义、异常处理及 VC-LTL／YY-Thunks 的 XP／Win7 兼容配置。
Actions 不编译、不运行测试；本次新增的测试仅位于本地 `rebuild/tests/dll_size_versions`，不进入发行包。

本地构建、补丁号分配及版本资源说明见 [日期版本构建](docs/BUILD_VERSION.md)。
