# 开发文档

## 1. 项目概述

CryptoPrice 是面向 TrafficMonitor 的 Windows C++ 多交易所行情插件，当前版本为 2.0.0。它通过统一模型接入 Bitget、OKX 和 Binance 的公共现货与合约行情。

核心约束：

- 最多配置 16 个标的；
- 数据源为全局设置，切换后所有标的使用同一家交易所；
- “显示价格”选项全局作用于所有合约，可选择最新价格、标记价格或指数价格；现货只显示最新成交价；
- 合约支持最新价、标记价和指数价；
- 网络请求必须在后台线程执行；
- 保留旧显示项 ID 和旧 INI 读取能力。

## 2. 开发环境

- Windows 10 或更高版本；
- Visual Studio 2022 C++ 桌面开发工作负载；
- Windows 10/11 SDK；
- CMake 3.20 或更高版本；
- C++17。

运行时只依赖 WinHTTP、User32、GDI32 和 Comctl32，不使用 MFC 或第三方 JSON 库。

## 3. 目录结构

| 路径 | 职责 |
| --- | --- |
| `src/PluginInterface.h` | TrafficMonitor API v7 ABI |
| `src/BitgetPricePlugin.*` | 插件入口、显示项、后台调度和格式化；文件名为兼容历史保留 |
| `src/MarketClient.*` | 通用 HTTP 客户端及三家交易所适配 |
| `src/Json.*` | Bitget、OKX、Binance 响应解析和符号规范化 |
| `src/Config.*` | 数据源、标的、价格类型及 INI 持久化 |
| `src/OptionsWindow.*` | 纯 Win32 设置窗口 |
| `tests/Tests.cpp` | 三家响应解析和配置单元测试 |
| `tests/PluginSmoke.cpp` | DLL ABI、配置迁移和实时行情测试 |
| `tests/fixtures*` | Bitget、OKX、Binance 和旧版配置样本 |

## 4. 运行流程

```text
TrafficMonitor::DataRequired
        │
        ├─ 应用上一轮 pending 结果
        └─ 到达刷新时间后复制当前 Settings
                    │
                    └─ 后台线程按市场类别分组
                              │
                              └─ MarketClient
                                  ├─ Bitget adapter
                                  ├─ OKX adapter
                                  └─ Binance adapter
                                        │
                                        └─ 统一 Ticker 缓存
```

设置变化时 generation 递增。旧设置启动的迟到响应会被丢弃，避免切换数据源后短暂显示上一家交易所的数据。

## 5. 统一数据模型

### DataSource

| 枚举 | INI 值 | 显示名 |
| --- | --- | --- |
| `Bitget` | `bitget` | Bitget |
| `Okx` | `okx` | OKX |
| `Binance` | `binance` | Binance |

### 市场类别

| 内部类别 | 含义 | 标准输入 |
| --- | --- | --- |
| `SPOT` | 现货 | `BTCUSDT` 或 `BTC/USDT` |
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

插件访问 OKX 时按以下顺序回退，适配 DNS 被污染或需要代理出口的网络：

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

## 7. 显示项规则

- `GetItem(0)` 是汇总项；其 ID 继续使用 `BitgetContractPrice0`，确保旧用户升级后无需重新勾选。
- `GetItem(1..N)` 是各标的独立项；ID 继续使用 `BitgetContractPriceItem0..15`。
- 汇总项标签会随数据源显示为 `Bitget:`、`OKX:` 或 `Binance:`。
- `GetItemValueText()` 只返回缓存，不能访问网络。
- 没有配置标的时仍返回占位项。

内部类和显示项 ID 中保留 Bitget 字样属于兼容实现，不影响新产物名称和插件显示名。

## 8. 配置文件与迁移

新配置文件为 TrafficMonitor 插件配置目录下的 `CryptoPrice.ini`：

```ini
[General]
DataSource=okx
RefreshSeconds=5
ShowChange=1
PriceType=mark
Count=2

[Instrument0]
Category=USDT-FUTURES
Symbol=BTCUSDT

[Instrument1]
Category=SPOT
Symbol=ETHUSDT
```

规则：

- `DataSource` 可为 `bitget`、`okx`、`binance`；
- `RefreshSeconds` 范围为 2–300；
- `PriceType` 可为 `last`、`mark`、`index`；
- `Count` 范围为 0–16；
- 现货忽略 `mark` 或 `index` 设置，始终使用最新价。

迁移逻辑：如果 `CryptoPrice.ini` 不存在但发现 `BitgetPrice.ini`，插件会读取旧配置并默认使用 Bitget。用户在选项窗口保存后会写入新的 `CryptoPrice.ini`。

## 9. 构建

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

产物为 `CryptoPrice.dll`。插件 DLL 与 TrafficMonitor 必须使用相同架构。

## 10. 修改检查清单

- 不改变官方 API v7 虚表顺序；
- 不在显示项 getter 或绘制回调中请求网络；
- 新数据源必须将 symbol、涨跌幅和价格字段转换为统一模型；
- 现货不能读取标记价或指数价；
- 币本位永续不能与日期交割合约混淆；
- 切换数据源后必须丢弃旧 generation 响应；
- 保持 `BitgetContractPrice*` ID 和旧配置迁移；
- 新接口补充离线样本、真实行情测试和部署域名。

## 11. 参考资料

- [TrafficMonitor 插件开发指南](https://github.com/zhongyang219/TrafficMonitor/wiki/%E6%8F%92%E4%BB%B6%E5%BC%80%E5%8F%91%E6%8C%87%E5%8D%97)
- [Bitget UTA Tickers](https://www.bitget.com/zh-CN/api-doc/uta/public/Tickers)
- [OKX Market Data](https://www.okx.com/docs-v5/en/#rest-api-market-data-get-tickers)
- [Binance Spot Market Data](https://developers.binance.com/docs/binance-spot-api-docs/rest-api/market-data-endpoints)
- [Binance Futures Market Data](https://developers.binance.com/docs/derivatives/usds-margined-futures/market-data/rest-api)
