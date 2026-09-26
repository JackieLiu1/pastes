# Pastes

简体中文 | [English](./README.md)

![Release-CI](https://github.com/JackieLiu1/pastes/workflows/Release-CI/badge.svg) ![C/C++ CI](https://github.com/JackieLiu1/pastes/workflows/C/C++%20CI/badge.svg)

感谢您选择 Pastes 程序。

Pastes 是一款跨平台的剪贴板管理器。Windows 上可以按 `Win+V` 唤出底部剪贴板历史面板，也可以点击托盘图标。程序运行时接管 `Win+V`，退出后恢复 Windows 原生剪贴板历史；不修改系统设置。若快捷键接管失败，会尝试 `Ctrl+Shift+V`，实际可用的快捷键会显示在面板底部和托盘菜单中。Linux 使用 `Ctrl+Shift+V`。

Pastes 保留最近 7 天的剪贴板历史。用左右方向键或 `Tab` / `Shift+Tab` 在可见卡片间前后循环浏览；按 `Ctrl+F` 进入搜索框，也可以直接输入搜索。按 `Enter`、双击卡片或按 `Ctrl+1` 至 `Ctrl+9` 取用对应条目。按住 `Shift` 可粘贴为纯文本，`Space` 预览，`Delete` 删除，`Ctrl+C` 仅复制到剪贴板。Windows 上取用条目后会尝试粘贴到唤出面板前的窗口；目标窗口无法恢复焦点时，内容仍会留在系统剪贴板。面板高度按所在屏幕自动确定，不支持拖动拉伸。

按 `Space` 打开与面板主题一致的预览窗口，可选择长文本并完整查看图片。按 `Esc` 或 `Space` 关闭，也可以用窗口底部的按钮复制或粘贴内容。

![img](./pastes-view.gif)

## 界面风格

以 Paste 的底部面板与横向卡片浏览为基础，Pastes 使用自己的叠卡图标、暖白与墨绿配色、青绿色选中描边和内容类型标签。浅色与深色主题可从面板右上角菜单切换。界面由 C++ / Qt Widgets 实现，无 QML 运行依赖。
