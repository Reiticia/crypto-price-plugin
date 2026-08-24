# 开发文档

## 1. 项目概述

BitgetPrice 是一个面向 TrafficMonitor 的 Windows C++ 插件，用于显示 Bitget UTA 合约行情。当前版本为 1.1.0。

主要能力：

- 最多配置 16 个合约；
- 支持 `USDT-FUTURES`、`USDC-FUTURES` 和 `COIN-FUTURES`；
- 支持最新成交价、标记价格和指数价格；
- 价格和 24 小时涨跌幅统一保留两位小数；
- 第一个显示项汇总全部合约，后续显示项分别对应单个合约；
- 后台请求行情，不阻塞 TrafficMonitor 绘制线程。

## 2. 开发环境

- Windows 10 或更高版本；
- Visual Studio 2022，“使用 C++ 的桌面开发”工作负载；
- Windows 10/11 SDK；
- CMake 3.20 或更高版本；
- C++17。

插件仅使用系统库：WinHTTP、User32、GDI32 和 Comctl32，不依赖 MFC 或第三方 JSON 库。

## 3. 目录结构

| 路径 | 职责 |
| --- | --- |
| `src/PluginInterface.h` | TrafficMonitor 官方插件 API v7 ABI |
| `src/BitgetPricePlugin.*` | 插件入口、显示项、刷新调度和行情格式化 |
| `src/BitgetClient.*` | WinHTTP 请求及字符编码转换 |
| `src/Json.*` | Bitget ticker 响应解析 |
| `src/Config.*` | 设置模型、交易对规范化及 INI 持久化 |
| `src/OptionsWindow.*` | 纯 Win32 配置窗口 |
| `src/dllmain.cpp` | DLL 入口和模块句柄 |
| `tests/Tests.cpp` | 配置与 JSON 解析单元测试 |
| `tests/PluginSmoke.cpp` | DLL ABI、多显示项及实时行情烟雾测试 |
| `tests/fixtures/BitgetPrice.ini` | 双合约测试配置 |

## 4. 运行架构

```text
TrafficMonitor::DataRequired
        │
        ├─ 应用上一轮后台请求结果
        └─ 到达刷新时间后启动工作线程
                    │
                    ├─ 按合约类别分组
                    ├─ 每种类别请求一次 Bitget UTA ticker
                    ├─ 解析并匹配用户配置的 symbol
                    └─ 写入 pending 结果
                              │
                              └─ 下一次 DataRequired 更新显示缓存
```

同一类别无论配置多少合约都只请求一次 `/api/v3/market/tickers`，随后在本地匹配交易对。这可以减少请求次数，并避免触碰 Bitget 的 IP 限频。

### 显示项规则

- `GetItem(0)`：合约汇总项，ID 固定为 `BitgetContractPrice0`。保留该 ID 是为了让旧版本已启用的第一个任务栏项目在升级后自动变成汇总显示。
- `GetItem(1..N)`：每个合约的独立显示项，ID 为 `BitgetContractPriceItem0..15`。
- 没有配置合约时仍返回一个占位项，提示用户打开插件选项。
- `GetItemValueText()` 只返回缓存文本，绝不能在此方法内请求网络。

## 5. TrafficMonitor 生命周期

| 回调 | 项目中的处理 |
| --- | --- |
| `TMPluginGetInstance` | 返回进程内唯一插件实例 |
| `OnInitialize` | 获取 TrafficMonitor 插件配置目录 |
| `OnExtenedInfo(EI_CONFIG_DIR)` | 加载 `BitgetPrice.ini` 并触发刷新 |
| `GetItem` | 返回汇总项或独立合约显示项 |
| `DataRequired` | 应用缓存结果并按间隔调度后台刷新 |
| `ShowOptionsDialog` | 打开多合约配置窗口并保存设置 |
| `GetTooltipInfo` | 返回所有合约的完整行情或错误信息 |
| `OnPluginCommand` | 执行“立即刷新 Bitget 行情”命令 |

`PluginInterface.h` 的虚函数顺序决定 DLL ABI。升级官方接口时应整体同步官方头文件，不能只插入或删除某个虚函数。

## 6. Bitget API

请求：

```http
GET https://api.bitget.com/api/v3/market/tickers?category=USDT-FUTURES
```

支持的 `category`：

| 配置值 | 含义 |
| --- | --- |
| `USDT-FUTURES` | USDT 本位合约 |
| `USDC-FUTURES` | USDC 本位合约 |
| `COIN-FUTURES` | 币本位合约 |

使用的响应字段为 `symbol`、`lastPrice`、`markPrice`、`indexPrice`、`price24hPcnt` 和 `ts`。公共行情接口不需要 API Key。

网络超时设置为连接、发送各 3 秒，接收 6 秒；单次响应上限为 16 MiB。请求失败时显示值为 `--`，详细原因写入 tooltip。

## 7. 配置文件

配置文件位于 TrafficMonitor 提供的插件配置目录，文件名为 `BitgetPrice.ini`：

```ini
[General]
RefreshSeconds=5
ShowChange=1
PriceType=last
Count=2

[Instrument0]
Category=USDT-FUTURES
Symbol=BTCUSDT

[Instrument1]
Category=USDT-FUTURES
Symbol=ETHUSDT
```

规则：

- `RefreshSeconds` 范围为 2–300；
- `PriceType` 可为 `last`、`mark` 或 `index`；
- `Count` 范围为 0–16；
- symbol 会转成大写，并移除空格、`/`、`-` 和 `_`；
- 无配置文件时默认监控 `USDT-FUTURES/BTCUSDT`。

## 8. 构建

x64：

```powershell
cmake -S . -B build-x64 -A x64
cmake --build build-x64 --config Release
```

Win32：

```powershell
cmake -S . -B build-win32 -A Win32
cmake --build build-win32 --config Release
```

输出分别位于：

- `build-x64/Release/BitgetPrice.dll`
- `build-win32/Release/BitgetPrice.dll`

插件架构必须与 TrafficMonitor 主程序一致。

## 9. 修改检查清单

- 不改变官方 API v7 虚表顺序；
- 不在 `GetItem*` 或绘制回调中访问网络；
- 后台线程结束前不能卸载 DLL；
- 设置变化后递增 generation，丢弃旧配置对应的迟到响应；
- 第一个显示项 ID 保持不变，以兼容已有 TrafficMonitor 设置；
- 新字段同时补充 INI 读写、选项窗口和测试；
- 修改后按《测试文档》完成离线和实时验证。

## 10. 参考资料

- [TrafficMonitor 插件开发指南](https://github.com/zhongyang219/TrafficMonitor/wiki/%E6%8F%92%E4%BB%B6%E5%BC%80%E5%8F%91%E6%8C%87%E5%8D%97)
- [TrafficMonitor Stock 插件](https://github.com/zhongyang219/TrafficMonitorPlugins/tree/main/Plugins/Stock)
- [Bitget UTA 获取行情数据](https://www.bitget.com/zh-CN/api-doc/uta/public/Tickers)
