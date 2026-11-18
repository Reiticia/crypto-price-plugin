# 开发文档

## 1. 项目概述

CryptoPrice 是面向 TrafficMonitor 的 Windows C++ 多交易所行情插件，当前版本为 2.0.0。它通过统一模型接入 Bitget、OKX 和 Binance 的公共现货与合约行情。

核心约束：

- 最多配置 16 个标的；
- 数据源按标的独立设置；`US-STOCK` 固定使用 Kraken xStocks 的 USD 交易对；
- “显示价格”选项全局作用于所有合约，可选择最新价格、标记价格或指数价格；现货只显示最新成交价；
- 合约支持最新价、标记价和指数价；
- 网络请求必须在后台线程执行；
- 保留旧显示项 ID 和旧 INI 读取能力。

## 2. 开发环境

- Windows 10 或更高版本；
- Visual Studio 2022 C++ 桌面开发工作负载；
- Windows 10/11 SDK；
- CMake 3.21 或更高版本；
- C++17。

运行时只依赖 WinHTTP、User32、GDI32 和 Comctl32，不使用 MFC 或第三方 JSON 库。

## 3. 目录结构

| 路径 | 职责 |
| --- | --- |
| `src/PluginInterface.h` | TrafficMonitor API v7 ABI |
| `src/CryptoPricePlugin.*` | 插件入口、显示项、后台调度和格式化 |
| `src/MarketClient.*` | REST 设置校验、HTTP 轮询、手动诊断及推送元数据 |
| `src/StreamClient.*` | WebSocket / HTTP 模式互斥会话、心跳、重连、轮询及跨线程缓存 |
| `src/StreamProtocol.*` | 五家订阅协议、快照与增量合并 |
| `src/JsonValue.h` | 严格 JSON 解析器 |
| `src/Json.*` | Bitget、OKX、Binance 响应解析和符号规范化 |
| `src/Config.*` | 数据源、标的、价格类型及 INI 持久化 |
| `src/OptionsWindow.*` | 纯 Win32 设置窗口、后台标的校验及提交控制 |
| `src/InstrumentValidation.*` | 按市场分组查询并校验标的和所选价格，可注入查询函数离线测试 |
| `tests/Tests.cpp` | 三家响应解析和配置单元测试 |
| `tests/PluginSmoke.cpp` | DLL ABI 和实时行情测试 |
| `tests/fixtures*` | Bitget、OKX、Binance 配置样本 |

## 4. 运行流程

```text
首次 DataRequired / 配置变更 / 手动刷新
        └─ StreamClient 按（交易所、市场）启动会话
              ├─ HTTP 模式：立即及定期 FetchCategory → 按 generation 发布 pending
              └─ WebSocket 模式：建立常驻连接
              ├─ 必要时 REST 查询标的元数据（不查询价格）
              ├─ WebSocket 快照与增量 → 连接私有缓存
              └─ 按 generation 发布至 pending
后续每次 DataRequired → 读取 pending 最新推送 → 更新界面（无额外计时节流）
```

WebSocket 模式不轮询价格，也不设置显示间隔。网络线程收到推送即更新 pending，
宿主每次调用 `DataRequired` 时应用最新报价及错误；多条推送按标的合并为最新值。
网络线程不修改宿主可能正在读取的显示字符串，实际重绘频率由 TrafficMonitor 控制。
HTTP 模式使用同一 pending 发布路径；按组立即请求，然后按 `RefreshSeconds` 调度；
每组请求串行，慢请求结束后等待一个完整间隔，不积压补发。停止时中断等待，
已在途 HTTP 请求可自行完成，但回调与取消互斥，不得发布旧结果；两种模式互斥且不自动回退。
缺失快照告警每连接每标的只发布一次。
断线立即清空旧价；新连接独立缓存，Bybit 快照前的 delta 被忽略。每 15 秒发心跳，
75 秒无应用帧或 30 秒未取得任何标的更新则重连；安静／休市时用心跳确认存活。
重连按 1、2、4…30 秒退避，取价成功后复位，不退回 REST。设置变化递增 generation
并取消旧 session，停止回调与发布互斥，不在 UI 线程等待网络。
守卫通过 `WinHttpWebSocketClose` 取消阻塞接收，关闭握手最多等待 1 秒；接收返回且守卫退出后，
才由持有者释放 WebSocket 句柄，禁止并发 `WinHttpCloseHandle` 与接收调用。

`StreamProtocol` 生成订阅并合并字段；`StreamClient` 使用 WinHTTP WebSocket，处理 UTF-8
分片、2MB 消息上限、服务端关闭、订阅错误及重订阅。首次网络启动前 pin DLL；单例
不在 DllMain 析构。宿主没有 pre-unload API，启动推送后不支持进程内热卸载／替换，
必须退出 TrafficMonitor。进程退出只置原子停止标记，禁止在 loader lock 下 join。

设置窗口添加或更新标的时，通过后台任务按当前数据源、市场类型和价格类型查询行情。只有标的存在且所选价格为有限正数时才修改草稿；现货始终验证最新价。点击确定时重新校验完整列表，同一（数据源、市场类型）只查询一次，避免切换数据源或价格类型后绕过校验。查询失败不提交设置；校验期间禁用编辑和重复提交，取消时丢弃结果并等待后台请求结束后安全关闭窗口。

## 5. 统一数据模型

### DataSource

| 枚举 | INI 值 | 显示名 |
| --- | --- | --- |
| `Bitget` | `bitget` | Bitget |
| `Okx` | `okx` | OKX |
| `Binance` | `binance` | Binance |
| `Kraken` | `kraken` | Kraken / Kraken xStocks |
| `Bybit` | `bybit` | Bybit |

### 市场类别

| 内部类别 | 含义 | 标准输入 |
| --- | --- | --- |
| `SPOT` | 现货 | `BTCUSDT` 或 `BTC/USDT` |
| `USD-FUTURES` | Kraken 多抵押品 USD 永续（PF） | `BTCUSD`、`ETHUSD` |
| `US-STOCK` | 代币化美股／ETF（Kraken xStocks） | `AAPL`、`TSLA` 或 `AAPL/USD` |
| `USDT-FUTURES` | USDT 本位永续 | `BTCUSDT` |
| `USDC-FUTURES` | USDC 本位永续 | `BTCUSDC` |
| `COIN-FUTURES` | 币本位永续 | `BTCUSD` |

内部 symbol 会移除空格、`/`、`-`、`_` 并转成大写。展示时现货按计价币拆成 `BTC/USDT`，合约保持 `BTCUSDT`。

### Ticker

统一字段：

- `symbol`
- `lastPrice`
- `markPrice`
- `indexPrice`
- `price24hPcnt`，统一为小数比例，例如 `0.025` 表示 `2.5%`
- `timestamp`

Binance 返回的 `priceChangePercent` 是百分数，解析器会除以 100；OKX 使用 `last` 和 `open24h` 计算比例；Bitget 的 `price24hPcnt` 可以直接使用。

## 6. 交易所适配

运行时使用以下 WSS 端点（均为 TLS 443）：

| 数据源 | 端点 | 频道 |
| --- | --- | --- |
| Bitget UTA | `wss://ws.bitget.com/v3/ws/public` | `ticker` |
| OKX | `wss://ws.okx.com/ws/v5/public` | `tickers`、`mark-price`、`index-tickers` |
| Binance Spot | `wss://stream.binance.com/ws` | `@ticker` |
| Binance USDⓈ-M / COIN-M | `wss://fstream.binance.com/market/ws` / `wss://dstream.binance.com/market/ws` | `@ticker`、`@markPrice@1s` |
| Kraken Spot / xStocks | `wss://ws.kraken.com/v2` | `ticker`，trades 触发 |
| Kraken Futures | `wss://futures.kraken.com/ws/v1` | `ticker`、`heartbeat` |
| Bybit | `wss://stream.bybit.com/v5/public/{spot,linear,inverse}` | `tickers.{真实标的}` |

下面的 REST 端点用于 HTTP 轮询、设置校验、显式诊断和元数据；WebSocket 模式不轮询价格。


### Bitget

```http
GET https://api.bitget.com/api/v3/market/tickers?category={category}
```

ticker 响应已同时包含最新价、标记价和指数价。

### OKX

```http
GET https://www.okx.com/api/v5/market/tickers?instType=SPOT|SWAP
GET https://www.okx.com/api/v5/public/mark-price?instType=SWAP
GET https://www.okx.com/api/v5/market/index-tickers?quoteCcy=USDT|USDC|USD
```

REST 设置校验访问 OKX 时按以下顺序回退，适配 DNS 被污染或需要代理出口的网络：

1. 直连 `eea.okx.com`、`www.okx.com`（同一套 v5 公共接口），并记住本次运行中可用
   的入口；
2. 通过本地代理重试：先读当前用户的系统代理（`WinHttpGetIEProxyConfigForCurrentUser`），
   再尝试 Clash / v2rayN 常见端口 `7897`、`7890`、`10809`、`1080`，代理端自行解析域名；
3. 通过 DoH（`cloudflare-dns.com`/`1.1.1.1`、`dns.google`/`8.8.8.8`）解析 OKX 真实
   IP 后直连，绕过被污染的 DNS。

OKX 的 `BTC-USDT-SWAP` 会规范化成 `BTCUSDT`。只有选择标记价或指数价时才请求对应的附加接口。

### Binance

```http
GET https://api.binance.com/api/v3/ticker/24hr?type=MINI
GET https://fapi.binance.com/fapi/v1/ticker/24hr
GET https://fapi.binance.com/fapi/v1/premiumIndex
GET https://dapi.binance.com/dapi/v1/ticker/24hr
GET https://dapi.binance.com/dapi/v1/premiumIndex
```

USDⓈ-M 接口同时承载 USDT 和 USDC 合约。币本位只保留 `_PERP` 永续，例如 `BTCUSD_PERP` 规范化成 `BTCUSD`，日期交割合约不会混入。

### Kraken 加密行情与 Bybit

Kraken Spot 使用 `/0/public/AssetPairs?aclass_base=currency` 与 `/0/public/Ticker` 联合解析，
按 `wsname` 规范化 `XBT→BTC`、`XDG→DOGE`，不猜测内部 X/Z 资产前缀。现货 `o` 是当日
开盘价，因此不生成 24 小时涨跌幅。`USD-FUTURES` 使用 `futures.kraken.com/derivatives/api/v3/tickers`，
仅接受未暂停的 `PF_` 永续，读取数值型 last/mark/index，按 open24h 计算涨跌。

Bybit 使用 `/v5/market/instruments-info`（分页、游标编码、最多 32 页）与 `/v5/market/tickers`。
按 category spot/linear/inverse、contractType、quoteCoin 过滤永续与结算币；USDC 原始
BTCPERP 用 baseCoin+quoteCoin 映射为 BTCUSDC。price24hPcnt 本身是比例，不再除以 100。
`ExchangeJson.cpp` 严格检查 JSON 容器、数字、缺字段及 API 错误，失败时清空部分结果。

### Kraken xStocks 美股

`GET https://api.kraken.com/0/public/Ticker?asset_class=tokenized_asset` 返回代币化资产。
仅解析规范的 `xUSD` 交易对，去掉后缀后按股票代码索引；排除 EUR、USDT 和 SPV 内部别名。
`c[0]` 为最新成交价；不使用标记价、指数价。`o` 是当日开盘价，不是 24 小时前价格，
因此不填充 `price24hPcnt`，也不为涨跌幅预留显示宽度。HTTP 模式使用 REST 最新价；WebSocket 模式按 `US-STOCK` 建立 v2 订阅，AssetPairs 元数据映射 `AAPLx/USD` 等真实标识。
配置存储原始代码（允许 1–16 位字母），显示时附加 `/USD`，数据源名称显示 Kraken xStocks。

## 7. 显示项规则

- `GetItem(0)` 是汇总项，ID 为 `CryptoPrice0`。
- `GetItem(1..N)` 是各标的独立项，ID 使用 `CryptoPriceItem0..15`；未配置槽位不会注册。
- TrafficMonitor 只在加载插件时枚举一次显示项，且没有动态增删显示项的插件接口。因此运行期间修改标的数量后，需要重启 TrafficMonitor 才能让“显示设置”的项目数量同步。
- 任务栏独立项仅显示 symbol，汇总标签固定为“行情”；设置列表、显示项名称和悬停提示保留来源，任务栏示例宽度不再为交易所名称占位。
- `GetItemValueText()` 只返回缓存，不能访问网络。
- 没有配置标的时汇总项仍返回占位内容。

## 8. 配置文件

新配置文件为 TrafficMonitor 插件配置目录下的 `CryptoPrice.ini`：

```ini
[General]
UpdateMode=websocket
RefreshSeconds=5
ShowChange=1
PriceType=mark
Count=2

[Instrument0]
DataSource=okx
Category=USDT-FUTURES
Symbol=BTCUSDT
Precision=2

[Instrument1]
DataSource=binance
Category=SPOT
Symbol=ETHUSDT
Precision=-1
```

规则：

- 每个 `[InstrumentN] DataSource` 可为 `bitget`、`okx`、`binance`、`kraken`、`bybit`；美股规范化为 `kraken`；
- 缺少独立数据源键时读取旧 `[General] DataSource`，再缺省为 Bitget；保存成功后移除旧全局键；
- 去重键为（数据源、市场、symbol），精度不影响去重；美股固定 Kraken；
- `UpdateMode` 为 `websocket`（缺省及未知值回退）或 `http`，两者互斥；
- `RefreshSeconds` 范围 2–300，默认 5，仅控制 HTTP 轮询，WebSocket 完全忽略该间隔；
- `PriceType` 可为 `last`、`mark`、`index`；
- `Count` 范围为 0–16；
- `Precision` 为每个标的独立的小数位数（-1 自动，0–10 固定），缺省或脏值回退 -1；
- 现货忽略 `mark` 或 `index` 设置，始终使用最新价。

## 9. 构建

默认构建链路为 `CMake → Visual Studio 2022 / MSBuild → MSVC CL.exe`。以下命令通过
Visual Studio 生成器创建工程，再由 MSBuild 调用 MSVC 编译器；当前不使用 Clang。

x64：

```powershell
cmake --preset x64
cmake --build --preset x64-release
```

Win32：

```powershell
cmake --preset win32
cmake --build --preset win32-release
```

产物为 `CryptoPrice.dll`。插件 DLL 与 TrafficMonitor 必须使用相同架构。

## 10. 修改检查清单

- 不改变官方 API v7 虚表顺序；
- 不在显示项 getter 或绘制回调中请求网络；
- 新数据源必须将 symbol、涨跌幅和价格字段转换为统一模型；
- 现货不能读取标记价或指数价；
- 币本位永续不能与日期交割合约混淆；
- 切换数据源后必须丢弃旧 generation 响应；
- 新接口补充离线样本、真实行情测试和部署域名。

## 11. 参考资料

- [TrafficMonitor 插件开发指南](https://github.com/zhongyang219/TrafficMonitor/wiki/%E6%8F%92%E4%BB%B6%E5%BC%80%E5%8F%91%E6%8C%87%E5%8D%97)
- [Bitget UTA Tickers](https://www.bitget.com/zh-CN/api-doc/uta/public/Tickers)
- [OKX Market Data](https://www.okx.com/docs-v5/en/#rest-api-market-data-get-tickers)
- [Binance Spot Market Data](https://developers.binance.com/docs/binance-spot-api-docs/rest-api/market-data-endpoints)
- [Binance Futures Market Data](https://developers.binance.com/docs/derivatives/usds-margined-futures/market-data/rest-api)
