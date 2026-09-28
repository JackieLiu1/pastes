# AGENTS.md

面向 AI 编码代理（以及新加入的人类协作者）的项目工作指南。

## 项目概览

Pastes 是一个跨平台剪贴板管理器：Windows 用 `Win+V`、Linux 用 `Ctrl+Shift+V` 或点击托盘唤出，保留最近 30 天的剪贴板历史，
macOS 用 `Shift+Cmd+V` 或点击菜单栏图标唤出。双击条目或按 `Enter`
复制回剪贴板，并由各平台的 PasteTarget 请求粘贴到原焦点应用。

- 语言/框架：C++17 + Qt6（Core / Gui / Widgets / Sql）
- 构建系统：CMake（≥ 3.16），不使用 qmake
- 平台：Windows（Win32 API）、Linux（X11/XRecord/XTest、gio）、macOS（AppKit / Carbon / Accessibility）
- 单实例：捆绑的 3rd/SingleApplication（静态库，源码直接编入，`QAPPLICATION_CLASS=QApplication`）
- 持久化：SQLite（QSQLITE），两张表 `item`（md5/imagedata/icondata/time）与
  `data`（md5/formats/format_data，每格式一行）

## 构建与验证

```bash
cmake -B build -S . -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

- Windows/MSYS2 工具链：`C:\msys64\ucrt64\bin`（ucrt64 环境，`qmake` 即 Qt6）。
  依赖 qt6-base、qt6-tools（lrelease）、psapi、gdi32。
- Linux 依赖：qt6-base-dev、qt6-tools-dev、libglib2.0-dev、libxtst-dev、libx11-dev、pkg-config（gio）。
- 本项目没有单元测试。验证手段：编译零错误 + 冒烟测试
  （启动程序 → `Set-Clipboard` 写入标记文本 → 约 2 秒后确认 `PastesDatabase.db`
  中出现该文本 → 重启程序确认历史加载不崩溃）。
- 数据库文件位置：Windows 在 exe 同目录，Linux 在 `~/.cache/PastesDatabase.db`。

## 架构与数据流

```
ClipboardSource::clipboardChanged
  → Qt 变化通知；macOS 另检查原生 pasteboard 计数
  → 记录来源：Windows 后台读取图标（QImage），macOS 保留来源应用
  → 快照定时器（等待时间由 ClipboardSource::settleInterval 决定）
  → MainWindow::clipboard_later()          # 快照、dup_mimedata、MD5、去重、建 PasteItem
      → Database::insertPasteItem()        # 队列化信号转发到工作线程
Database::Worker（单一持久线程，持有独立 QSqlDatabase 连接）
  → load/insert/remove 全部串行执行
MainWindow::parsingData()                  # 启动时从库加载，过滤已满 30 天与空条目
```

关键类：

- `MainWindow`（mainwindow.cpp）— 主窗口、剪贴板监听、条目生命周期。
- `AppDialog` / `AboutDialog`（appdialog.cpp）、`SettingsDialog`（settingsdialog.cpp）— 统一圆角弹窗；设置通过信号即时应用主题、快捷键提示与记录暂停，并由 QSettings 保存。暂停时取消防抖，不记录当时的剪贴板；恢复后仅记录新复制。
- `StartupIntegration`（platform/startupintegration.cpp 与各系统实现）— Windows 当前账户 Run 注册表项、Linux 用户 autostart 文件；以系统项为状态来源，写入失败须恢复开关并显示错误，不能只保存一个假状态。旧 Windows 安装的全用户项需要升级安装器清理。
- `Database` + `Database::Worker`（database.cpp）— 数据库门面 + 工作线程。
- `ClipboardSource`（platform/clipboardsource.h）— 统一剪贴板通知与来源图标接口，各系统实现位于 platform/{windows,macos,linux}/；异步结果用请求编号匹配仍存在的条目，迟到图标经 Database 更新。
- `PasteItem`（pasteitem.cpp）— 列表条目 widget；`copyData()` 复制回剪贴板，通过 QMimeData 的进程内属性保留原来源图标（不增加 MIME 格式），经同一防抖流程重新置顶；重复条目在来源查询为空时保留已有图标。
- `CardSwipeOverlay`（cardswipe.cpp）— GUI 线程缓存卡片快照，拖动可越过面板边界；松手后上沿向后翻倒至亮线，再向中心收成光点，散出短促粒子并熄灭，整体共 480 ms，未达到阈值时回弹；撤销从细线反向展开并落回卡槽，不播放关机闪光或粒子，快速撤销接续当前翻转姿态；删除在松手时提交，动画不持有剪贴板数据。
- `CardReflowOverlay`（cardreflow.cpp）— 删除或撤销前记录相邻卡片位置，实际列表更新后用缓存快照平移补位或让位；取消动画恢复真实卡片，连续操作接续当前视觉位置。
- `ElasticScrollController`（elasticscroll.cpp）— GUI 线程控制横向滚动的惯性与边界阻力/回弹，通过平移真实 viewport 保持卡片和点击位置一致；触控板不重复施加系统惯性，列表变化、搜索、导航或隐藏时取消运动。
- `StackedWidget`/`TextFrame`/`PixmapFrame`/`FileFrame`（pasteitemcontext.cpp）— 条目内容渲染。
- `GlobalShortcut`（platform/globalshortcut.h）— 全局唤出快捷键门面；
  后台细节在 platform/shortcut_p.h，系统实现位于各平台目录。
- `Platform` 的窗口、菜单、应用、路径和文件图标接口位于 platform/；
  原生句柄仅出现在系统实现中，公共 UI 不包含系统 SDK。详见 docs/platform-architecture.md。
- `ItemData`（pasteitem.h）— 条目数据的内存表示（mimeData/icon(QImage)/md5/time）。

## 线程规则（最重要）

1. **QPixmap 只能在 GUI 线程使用**。跨线程传图一律用 `QImage`
   （见 `ItemData::icon` 的类型注释）。
2. **QSqlDatabase 连接只能在创建它的线程使用**。所有 SQL 走
   `Database` 的队列化信号，禁止在别处直接 `addDatabase`。
3. `Database::Worker` 是唯一碰 SQL 的地方；新增数据库操作时给
   Worker 加槽函数 + Database 加转发信号，并注册所需 metatype。
4. `ItemData` 所有权：调用方持有，直到交给 `deletePasteItem()`
   （工作线程在删行后释放对象）。UI 删除条目时记得同步清掉
   `QListWidgetItem` 的 `Qt::UserRole` 引用和搜索恢复指针 `__current_item`。
5. 后台线程的标志位用 `std::atomic`（见 platform/shortcut_p.h）；阻塞的消息/事件循环
   退出必须显式唤醒（见 platform/windows/shortcut.cpp 的 PostThreadMessage(WM_QUIT)）。

## 编码风格

- 缩进：Tab；成员命名：类内 `m_` 前缀，MainWindow 保留历史风格 `__` 前缀。
- 函数无参写作 `(void)`；语句风格与现有文件保持一致，不要重排无关代码。
- Qt 连接：新代码用 PMF 语法 `QObject::connect(obj, &Class::sig, ...)`；
  带自定义类型的跨线程信号仍可用字符串语法（需 qRegisterMetaType）。
- 指针存入 QVariant 用 `uint64_t` 存取（Windows 的 `unsigned long` 是 32 位，
  会截断 64 位指针）。
- 禁止：`QApplication::processEvents()`（重入）、GUI 线程之外的 UI 操作、
  逐字节拼接 QByteArray 做哈希（用 `QCryptographicHash::addData`）。
- 平台代码放在 platform/{windows,macos,linux}/，由 platform/CMakeLists.txt
  选择实现；公共 UI 通过 platform/ 的接口调用，避免系统条件分支。跨 Qt 版本的
  API 用 `QT_VERSION_CHECK` 守卫（参考 platform/linux/windowblur.cpp）。
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
