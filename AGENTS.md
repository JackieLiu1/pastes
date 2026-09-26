# AGENTS.md

面向 AI 编码代理（以及新加入的人类协作者）的项目工作指南。

## 项目概览

Pastes 是一个跨平台剪贴板管理器：Windows 用 `Win+V`、Linux 用 `Ctrl+Shift+V` 或点击托盘唤出，保留最近 7 天的剪贴板历史，
双击条目或按 `Enter` 复制回剪贴板（Linux 下还会直接注入焦点窗口）。

- 语言/框架：C++17 + Qt6（Core / Gui / Widgets / Sql）
- 构建系统：CMake（≥ 3.16），不使用 qmake
- 平台：Windows（Win32 API）、Linux（X11/XRecord/XTest、gio）、macOS 仅残缺支持
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
QClipboard::dataChanged
  → 防抖定时器（1s，CLIPBOARD_SETLE_MS）
  → MainWindow::clipboard_later()          # 快照、dup_mimedata、MD5、去重、建 PasteItem
      → Database::insertPasteItem()        # 队列化信号转发到工作线程
Database::Worker（单一持久线程，持有独立 QSqlDatabase 连接）
  → load/insert/remove 全部串行执行
MainWindow::parsingData()                  # 启动时从库加载，过滤 >7 天与空条目
```

关键类：

- `MainWindow`（mainwindow.cpp）— 主窗口、剪贴板监听、条目生命周期。
- `Database` + `Database::Worker`（database.cpp）— 数据库门面 + 工作线程。
- `PasteItem`（pasteitem.cpp）— 列表条目 widget；`copyData()` 复制回剪贴板。
- `StackedWidget`/`TextFrame`/`PixmapFrame`/`FileFrame`（pasteitemcontext.cpp）— 条目内容渲染。
- `GlobalShortcut`/`ShortcutPrivate`（shortcut*.cpp）— 全局唤出快捷键，
  平台实现分文件（shortcut_win.cpp / shortcut_x11.cpp）。
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
5. 后台线程的标志位用 `std::atomic`（见 shortcut.h）；阻塞的消息/事件循环
   退出必须显式唤醒（见 shortcut_win.cpp 的 PostThreadMessage(WM_QUIT)）。

## 编码风格

- 缩进：Tab；成员命名：类内 `m_` 前缀，MainWindow 保留历史风格 `__` 前缀。
- 函数无参写作 `(void)`；语句风格与现有文件保持一致，不要重排无关代码。
- Qt 连接：新代码用 PMF 语法 `QObject::connect(obj, &Class::sig, ...)`；
  带自定义类型的跨线程信号仍可用字符串语法（需 qRegisterMetaType）。
- 指针存入 QVariant 用 `uint64_t` 存取（Windows 的 `unsigned long` 是 32 位，
  会截断 64 位指针）。
- 禁止：`QApplication::processEvents()`（重入）、GUI 线程之外的 UI 操作、
  逐字节拼接 QByteArray 做哈希（用 `QCryptographicHash::addData`）。
- 平台代码集中在 `#ifdef Q_OS_WIN / Q_OS_LINUX` 块内；跨 Qt 版本的 API
  用 `QT_VERSION_CHECK` 守卫（参考 mainwindow.cpp 的 KWindowEffects）。
- 中文注释/提交信息可用；面向用户的字符串必须走 `QObject::tr()`
  （新增源码记得加入 CMakeLists.txt 并 `lupdate` 更新 Pastes_zh_CN.ts）。

## 性能红线

- `resizeEvent()` 里禁止无缓存的平滑缩放、新建 GraphicsEffect、
  `setStyleSheet` 重解析——先查缓存再干活（参考 FileFrame/Barnner 的做法）。
- 启动路径上不做非必要的图片解码（Database::load 仅在条目确有图片格式时解码）。
- 遍历像素时用 `constScanLine`，不要 `image.pixel(x, y)`。

## 提交规范

- **按功能一个一个提交**：一个提交只做一件事（修复/重构/构建/CI 分开），
  先编译通过再提交，提交信息用中文、首行祈使句概述 + 正文讲清为什么。
- 不要在一个提交里混入无关的格式化变动。

## 已知取舍

- Qt6 的 Linux 构建没有窗口模糊效果（KWindowEffects 只存在于 KF5），
  相关代码已用版本守卫编译排除。
- 数据库 schema 保持向后兼容，勿轻改 `item`/`data` 表结构；
  图片去重 MD5 算法在 2026-09 更换过一次，旧条目首次重复复制会多留一条。
- 多显示器/HiDPI 场景目前只处理主屏，属已知未修复项。
