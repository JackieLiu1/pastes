# 历史加载性能基准

基准用于区分数据库加载和卡片创建的成本，并保留可以复测的原始数据。
它是默认关闭的开发工具，不加入 CTest 的耗时门槛，也不随应用安装。

## 构建与运行

```sh
cmake -B build/performance -S . -DCMAKE_BUILD_TYPE=Release \
    -DPASTES_BUILD_BENCHMARKS=ON
cmake --build build/performance --target history_benchmark --parallel 4
python3 benchmarks/run-history-benchmark.py \
    build/performance/benchmarks/history_benchmark \
    --label baseline --output /tmp/pastes-baseline.json
```

Windows 的可执行文件带 `.exe`；多配置生成器还需要选择 `Release` 目录。
运行器使用 Python 3 标准库。默认使用 Qt offscreen 平台；每个规模、数据类型和阶段
运行五个独立进程，输出各阶段耗时的中位数、P95、最小值、最大值及全部原始样本。
一次性阶段指标只有五个样本，其 P95 是其中最大值，应增加重复次数再判断偶发抖动。
每个界面进程还执行五轮搜索，各查询类型会汇总二十五个耗时样本。

```sh
# 只复测数据库，或缩小数据规模
python3 benchmarks/run-history-benchmark.py \
    build/performance/benchmarks/history_benchmark \
    --counts 100 1000 --stages database --repeats 10 \
    --label indexed --output /tmp/pastes-indexed.json
```

测量使用相同工具链、Qt、优化选项和机器；执行期间避免同时编译、运行另一组基准。
内存安全检查单独运行，避免检测开销混入性能数据。

## 数据与隔离

- 默认规模为 100、1,000、10,000 条历史，每条保留二进制自定义 MIME 数据。
- `text` 全部为约 180 字符的中英文文本，10% 为收藏。
- `mixed` 包含 70% 普通文本、20% HTML、5% 额外 64 Ki 字符的长文本、
  5% 320×180 图片，10% 为收藏。PNG 为不同的纯色合成图；不能据此推断
  高分辨率照片、灰度/HDR 图像或大文件的性能。
- 每个进程新建 `QTemporaryDir` 下的 SQLite 文件；测试前没有内容索引。
  每四条记录使用相同时间戳，验证同时间条目的排序。准备数据使用单个事务，
  不计入加载时间；生产读取全部走真实 `Database` 和其 SQL 工作线程。
- 设置使用独立的临时 INI 目录，不读取用户历史、系统剪贴板或用户设置，
  不创建全局快捷键、托盘、同步连接。临时数据随进程正常退出删除。
- 每次加载检查条目数量、顺序、二进制 MIME、收藏和图片仍为编码形式；
  每次搜索检查匹配数量，防止通过丢弃工作取得更短耗时。

## 指标含义

| 字段 | 测量边界 |
| --- | --- |
| `first_open_load_ms` | 从构造真实仓库到收到已还原的历史值；包含打开连接、建索引和加载 |
| `reopen_load_ms` | 关闭仓库后，再次打开同一文件并加载；包含打开连接，已有索引可直接使用 |
| `card_bind_ms` | 从创建 HistoryView 到服务接受全部历史、创建卡片、完成筛选和列表布局 |
| `offscreen_render_ms` | 卡片绑定后，通过 `grab()` 绘制可见视图的 CPU 耗时 |
| `search_samples` | 设置搜索文本到同步筛选和列表布局完成；包含英文、中文、无结果、长文本尾部及清空搜索 |
| `peak_rss_kib` | 独立进程的最大驻留内存；包括 Qt 初始化、数据准备、两次加载，以及该阶段的界面工作 |
| `database_bytes` | 打开后临时数据库大小，包含新建索引 |
| `data_lookup_plan` | 真实 Qt SQLite 驱动中内容查询的执行计划 |

`database` 阶段不创建卡片，`view` 阶段完成同样的数据库测量后，再把所得条目
通过快照仓库交给真实 HistoryService 和 HistoryView，分开记录卡片成本。
进程内存峰值在 macOS 使用系统 `time -l`，Linux 使用 GNU `time`；
没有该工具的 Linux 或 Windows 返回空值。需要允许读取系统进程统计。

这里的第一次打开也使用刚刚写入的临时文件，**不是磁盘缓存被清空的冷启动**。
独立进程隔离 Qt 对象和分配器状态，无法隔离系统文件缓存。
offscreen 绘制不是原生窗口呈现；这些数值不包含应用入口、同步、全局快捷键、
窗口动画、合成器延迟、触控板或中文输入法交互。
界面阶段使用 1200×400 的普通 QWidget 容器和 Qt 默认字体、样式，
没有组装 MainWindow 的主题、阴影和原生玻璃效果；不能作为正式面板的完整耗时。
真实唤出、帧间隔和后台常驻需要另外通过原生运行及 Instruments 等工具测量。
