# AGENTS.md

面向 AI 编码代理（以及新加入的人类协作者）的项目工作指南。

## 项目概览

Pastes 是一个跨平台剪贴板管理器：Windows 用 `Win+V`、Linux 用 `Ctrl+Shift+V` 或点击托盘唤出，保留最近 30 天的剪贴板历史，
macOS 用 `Shift+Cmd+V` 或点击菜单栏图标唤出。双击条目或按 `Enter`
复制回剪贴板，并由各平台的 PasteTarget 请求粘贴到原焦点应用。

- 语言/框架：C++17 + Qt ≥ 6.3（Core / Gui / Widgets / Sql）
- 构建系统：CMake（≥ 3.16），不使用 qmake
- 平台：Windows（Win32 API）、Linux（X11/XRecord/XTest、gio）、macOS（AppKit / Carbon / Accessibility）
- 单实例：捆绑的 3rd/SingleApplication（静态库，源码直接编入，`QAPPLICATION_CLASS=QApplication`）
- 持久化：SQLite（QSQLITE），两张表 `item`（md5/imagedata/icondata/time）与
  `data`（md5/formats/format_data，每格式一行）

## 构建与验证

```bash
cmake -B build -S . -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

- Windows/MSYS2 工具链：`C:\msys64\ucrt64\bin`（ucrt64 环境，`qmake` 即 Qt6）。
  依赖 qt6-base、qt6-tools（lrelease）、psapi、gdi32。
- Linux 依赖：qt6-base-dev、qt6-tools-dev、libglib2.0-dev、libxtst-dev、libx11-dev、pkg-config（gio）。
- CTest 注册业务、数据库、剪贴板、历史视图及依赖边界五组测试，不需要 Qt Test 模块。
  GUI 组件测试使用 offscreen，只验证数据与命令，不能代替原生平台交互。
  验证手段：编译零错误、零新增警告 + CTest + 冒烟测试
  （启动程序 → `Set-Clipboard` 写入标记文本 → 约 2 秒后确认 `PastesDatabase.db`
  中出现该文本 → 重启程序确认历史加载不崩溃）。
- 数据库文件位置：Windows 在 exe 同目录，Linux 在 `~/.cache/PastesDatabase.db`。

## 架构与数据流

```
ClipboardSource / ClipboardFeed
  → ClipboardController                   # 原生同步、来源请求、等待、独立快照和复制
  → ClipboardContent::fingerprint          # 保持既有 MD5 输入
  → HistoryService                         # 去重、过期、顺序、撤销和请求身份
      → HistoryRepository / Database       # GUI 门面取得 StoredEntry 值快照
          → Database::Worker               # 串行 SQL、事务、图片编码；不读取界面对象
      → HistoryView / PasteItem             # 类型化条目绑定、筛选、选择和动画
```

目录分为 `app/`、`core/`、`application/`、`storage/`、`platform/`、`ui/`，各自有构建目标。
依赖方向和所有权详见 `docs/architecture.md`；`cmake/check-dependencies.cmake` 检查禁止的跨层 include。

关键类：

- `MainWindow`（ui/mainwindow.cpp）— 窗口显示、主题、菜单、弹窗和目标粘贴协调。
- `HistoryView`（ui/historyview.cpp）— 卡片绑定、筛选、导航、撤销入口和动画编排。
- `CardInteractionController`（ui/cardinteraction.cpp）— 鼠标状态、双击、浏览与上拖删除命令。
- `HistoryService`（application/historyservice.cpp）— 历史生命周期、去重、30 天清理、原位置撤销和异步请求匹配。
- `ClipboardController`（application/clipboardcontroller.cpp）— 采集、暂停/恢复、普通/纯文本复制；不依赖 UI 或 SQL。
- `HistoryRepository` / `ClipboardFeed`（application/）— 应用层拥有的仓库与原生通知接口；具体实现在入口注入。
- `AppDialog` / `AboutDialog`（ui/appdialog.cpp）、`SettingsDialog`（ui/settingsdialog.cpp）— 统一圆角弹窗；设置通过信号即时应用主题、快捷键提示与记录暂停，并由 QSettings 保存。暂停时取消防抖，不记录当时的剪贴板；恢复后仅记录新复制。
- `StartupIntegration`（platform/startupintegration.cpp 与各系统实现）— Windows 当前账户 Run 注册表项、Linux 用户 autostart 文件；以系统项为状态来源，写入失败须恢复开关并显示错误，不能只保存一个假状态。旧 Windows 安装的全用户项需要升级安装器清理。
- `Database` + `Database::Worker`（storage/database.cpp）— 仓库适配器 + SQL 工作线程；只传值快照，使用事务，退出排空写入并在原线程关闭连接。
- `ClipboardData`（core/clipboarddata.cpp）— 共享压缩原图、按需解码的 MIME 数据与最大 1024 像素的卡片缩略图；预览与复制仍读取原始尺寸，灰度和高精度格式不进行运行时压缩替换。
- `ClipboardSource`（platform/clipboardsource.h）— 统一剪贴板通知与来源图标接口，各系统实现位于 platform/{windows,macos,linux}/；异步结果用请求编号匹配仍存在的条目，迟到图标经 Database 更新。
- `PasteItem`（ui/pasteitem.cpp）— 卡片持有类型化 HistoryEntry；`copyData()` 发出命令，由 ClipboardController 写入剪贴板并保留原来源图标。
- `CardSwipeOverlay`（ui/cardswipe.cpp）— GUI 线程缓存卡片快照，拖动可越过面板边界；松手后上沿向后翻倒至亮线，再向中心收成光点，散出短促粒子并熄灭，整体共 480 ms，未达到阈值时回弹；撤销从细线反向展开并落回卡槽，不播放关机闪光或粒子，快速撤销接续当前翻转姿态；删除在松手时提交，动画不持有剪贴板数据。
- `CardReflowOverlay`（ui/cardreflow.cpp）— 删除或撤销前记录相邻卡片位置，实际列表更新后用缓存快照平移补位或让位；取消动画恢复真实卡片，连续操作接续当前视觉位置。
- `ElasticScrollController`（ui/elasticscroll.cpp）— GUI 线程控制横向滚动的惯性与边界阻力/回弹，通过平移真实 viewport 保持卡片和点击位置一致；触控板不重复施加系统惯性，列表变化、搜索、导航或隐藏时取消运动。
- `StackedWidget`/`TextFrame`/`PixmapFrame`/`FileFrame`（ui/pasteitemcontext.cpp）— 条目内容渲染。
- `GlobalShortcut`（platform/globalshortcut.h）— 全局唤出快捷键门面；
  后台细节在 platform/shortcut_p.h，系统实现位于各平台目录。
- `Platform` 的窗口、菜单、应用、路径和文件图标接口位于 platform/；
  原生句柄仅出现在系统实现中，公共 UI 不包含系统 SDK。详见 docs/platform-architecture.md。
- `ItemData` / `HistoryEntry`（core/itemdata.h）— GUI 线程的条目与共享所有权；EntryId 表示本次条目身份，MD5 表示内容身份。
- `StoredEntry`（storage/storedentry.h）— MIME 字节、QImage、编码图片、时间和指纹的跨线程值快照；不包含 QObject 或 UI 指针。

## 线程规则（最重要）

1. **QPixmap 只能在 GUI 线程使用**。跨线程传图一律用 `QImage`
   核心和应用层不包含 QWidget、QPixmap 或 SQL 头文件。
2. **QSqlDatabase 连接只能在创建它的线程使用**。所有 SQL 走
   `Database` 的队列化信号，禁止在别处直接 `addDatabase`。
3. `Database::Worker` 是唯一碰 SQL 的地方；新增数据库操作时给
   Worker 加槽函数 + Database 加转发信号，并注册所需 metatype。
4. `HistoryEntry` 使用共享所有权，ItemData 释放自己的 MIME 对象；所有访问和释放均在 GUI 线程。
   卡片不把数据裸指针存进 QVariant；视图移除卡片时同步清理搜索恢复项。
   工作线程只接收 StoredEntry 值快照，禁止读取或释放 GUI 的 ItemData / QMimeData。
5. 后台线程的标志位用 `std::atomic`（见 platform/shortcut_p.h）；阻塞的消息/事件循环
   退出必须显式唤醒（见 platform/windows/shortcut.cpp 的 PostThreadMessage(WM_QUIT)）。
6. 图片编码结果由 HistoryService 按唯一插入请求编号匹配仍存在的条目，不能仅匹配 MD5 或地址。
   删除/撤销或重新复制可能创建同内容的新对象；压缩字节不可变，副本通过隐式共享保留。

## 编码风格

- 缩进：Tab；成员命名：类内 `m_` 前缀，MainWindow 保留历史风格 `__` 前缀。
- 函数无参写作 `(void)`；语句风格与现有文件保持一致，不要重排无关代码。
- Qt 连接：新代码用 PMF 语法 `QObject::connect(obj, &Class::sig, ...)`；
  带自定义类型的跨线程信号仍可用字符串语法（需 qRegisterMetaType）。
- 数据身份使用 EntryId，卡片和预览使用类型化 HistoryEntry，不再增加 QVariant 裸指针存储。
- 禁止：`QApplication::processEvents()`（重入）、GUI 线程之外的 UI 操作、
  逐字节拼接 QByteArray 做哈希（用 `QCryptographicHash::addData`）。
- 平台代码放在 platform/{windows,macos,linux}/，由 platform/CMakeLists.txt
  选择实现；公共 UI 通过 platform/ 的接口调用，避免系统条件分支。
  只支持 Qt 6.3 及以上；新增代码使用 Qt 6.3 已有接口，不添加 Qt5 兼容分支，也不擅自提高最低版本。
- 中文注释可用；提交信息必须遵循下方的英文提交规范。
  面向用户的字符串必须走 `QObject::tr()`
  （新增源码记得加入 CMakeLists.txt 并 `lupdate` 更新 Pastes_zh_CN.ts）。

## 性能红线

- `resizeEvent()` 里禁止无缓存的平滑缩放、新建 GraphicsEffect、
  `setStyleSheet` 重解析——先查缓存再干活（参考 FileFrame/Barnner 的做法）。
- 启动路径上不做非必要的图片解码（Database::load 仅在条目确有图片格式时解码）。
- 遍历像素时用 `constScanLine`，不要 `image.pixel(x, y)`。

## 提交规范

- **按功能一个一个提交**：一个提交只做一件事（修复/重构/构建/CI 分开），
  先编译通过再提交，不混入无关的格式化变动。
- **所有提交信息必须使用英文**：标题、正文和 trailers 均不得使用中文。
- 遵循 Linux 内核提交风格：标题使用 `subsystem: imperative summary`，
  例如 `ui: animate neighboring cards after deletion`；使用祈使句概述具体改动，
  不写 `This patch ...`，Git 提交标题不加邮件用的 `[PATCH]` 前缀。
- **每行最多 80 个字符**，标题建议不超过 75 个字符，正文优先按 75 列换行；
  不把“80 字符”理解为必须补齐到 80 列。标题后空一行，正文与 trailers 间也空一行。
- 正文先交代问题及原因，再说明解决方式、行为影响和必要的验证结果；
  不编造测试、评审或故障归因，不仅罗列改动文件。
- **每个提交必须带 SOB**：末尾添加 `Signed-off-by: Real Name <email>`，
  使用真实贡献者身份；本项目通常用 `git commit -s` 自动加入提交人的签署。
  保留已有的有效贡献者签署，不重复添加同一人的 Signed-off-by。
- 引用其他提交时使用至少 12 位提交 ID 并附标题；只有确认引入问题的提交后
  才添加 `Fixes:`，不得编造 `Reviewed-by:`、`Tested-by:` 等 trailers。
- 修改提交说明前确认提交尚未推送，保存旧 tip；重写后检查每个提交的文件树、
  顺序、作者信息、行宽和 SOB，已推送历史的重写须获得明确授权。
- 参考 [Linux kernel submitting patches](https://docs.kernel.org/process/submitting-patches.html)。

## 已知取舍

- Qt6 的 Linux 构建没有窗口模糊效果（KWindowEffects 只存在于 KF5），
  相关代码已用版本守卫编译排除。
- 数据库 schema 保持向后兼容，勿轻改 `item`/`data` 表结构；
  图片去重 MD5 算法在 2026-09 更换过一次，旧条目首次重复复制会多留一条。
- 多显示器/HiDPI 场景目前只处理主屏，属已知未修复项。
