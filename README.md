# Crypto 行情插件

用于 [TrafficMonitor](https://github.com/zhongyang219/TrafficMonitor) 的多交易所现货与合约行情插件。可以在 Bitget、OKX、Binance、Kraken、Bybit 之间切换数据源，同时监控多个标的。

插件只调用交易所公共行情接口，不需要 API Key，也不会访问账户或执行交易。

## 项目文档

- [开发文档](docs/DEVELOPMENT.md)：架构、交易所适配、配置格式和构建方法
- [测试文档](docs/TESTING.md)：解析测试、DLL 烟雾测试和三家真实行情测试
- [部署文档](docs/DEPLOYMENT.md)：安装、从旧版升级、回滚和故障排查

## 功能

- 数据源按标的独立设置：可同时监控 Bitget、OKX、Binance、Kraken、Bybit 的报价，同一交易对可添加多个交易所
- 同时配置最多 16 个现货或合约标的
- 支持现货、USDT 本位、USDC 本位、币本位合约，以及 Kraken 的 USD 本位合约
- 支持“美股（USD）”：固定使用 Kraken xStocks 美元交易对，输入 `AAPL`、`TSLA`、`NVDA` 等代码；与加密标的可混合监控
- “显示价格”可选择最新价格、标记价格或指数价格；该选项全局作用于合约，现货始终显示最新成交价
- 合约显示为 `BTCUSDT`，现货显示为 `BTC/USDT`
- 加密行情的 24 小时涨跌幅保留两位小数；美股仅显示最新价，不提供 24 小时涨跌幅
- 价格小数位可配置：留空由程序按行情自动推断，也可固定为 0–10 位（每个标的独立设置）
- 默认任务栏项目汇总所有标的，同时提供各标的独立显示项
- 后台刷新行情，同一数据源和市场类别只请求一次
- 配置保存到 `CryptoPrice.ini`

## 统一市场映射

| 插件市场类型 | Bitget | OKX | Binance | Kraken | Bybit |
| --- | --- | --- | --- | --- | --- |
| 现货 | `SPOT` | `SPOT` | Spot | Spot | Spot |
| USDT 本位 | `USDT-FUTURES` | USDT Swap | USDⓈ-M Futures | 不支持 | Linear USDT Perpetual |
| USDC 本位 | `USDC-FUTURES` | USDC Swap | USDⓈ-M Futures | 不支持 | Linear USDC Perpetual |
| 币本位 | `COIN-FUTURES` | USD Swap | COIN-M Futures | 不支持 | Inverse Perpetual |
| USD 本位 | 不支持 | 不支持 | 不支持 | PF Multi-Collateral Perpetual | 不支持 |

例如币本位 `BTCUSD` 会自动映射为 OKX 的 `BTC-USD-SWAP` 或 Binance 的 `BTCUSD_PERP`。
Kraken 现货输入 `BTC/USD`、`BTC/USDT` 等实际交易对，`XBT` 自动映射为 `BTC`；
USD 本位合约输入 `BTCUSD`、`ETHUSD`（对应 `PF_XBTUSD` 等），不冒充 USDT/USDC 报价，
不包含交割及 PI 反向合约。Kraken 现货仅提供最新价，暂不显示滚动 24 小时涨跌幅；合约
支持最新价、标记价、指数价及 24 小时涨跌幅。Bybit USDC 合约输入 `BTCUSDC`，内部匹配
`BTCPERP` 等真实交易所代码；交割合约会被过滤。

“美股（USD）”的数据源固定为 Kraken xStocks，不受全局“显示价格”影响；显示为
`AAPL/USD`，不是 USDT 报价，也不按 USDT = USD 换算。行情为交易所的代币化美股／ETF
（xStocks），不是证券交易所原生报价，价格可能存在偏差；仅支持 Kraken 已上线的 USD 标的。
Kraken ticker 的开盘价是当日开盘价，不能用于推算滚动 24 小时涨跌幅，因此美股不显示百分比。

## 构建

需要 Visual Studio 2022（含“使用 C++ 的桌面开发”工作负载）和 CMake 3.21 或更高版本。

默认构建链路为 `CMake → Visual Studio 2022 / MSBuild → MSVC CL.exe`。CMake 负责生成
Visual Studio 工程，实际 C++ 编译由 MSVC 完成；当前不使用 Clang。

x64：

```powershell
cmake --preset x64
cmake --build --preset x64-release
ctest --preset x64-release
```

Win32：

```powershell
cmake --preset win32
cmake --build --preset win32-release
ctest --preset win32-release
```

产物为 `build/x64/Release/CryptoPrice.dll` 或 `build/win32/Release/CryptoPrice.dll`。

两个项目使用相同的 `CMakePresets.json` 约定，本地和 CI 的构建目录统一为
`build/<架构>/`，Release 的 DLL、测试程序位于其 `Release/` 子目录；Debug 则位于
`Debug/`。例如：

```text
build/
  x64/Release/CryptoPrice.dll
  win32/Release/CryptoPrice.dll
```

请从新目录获取产物；旧构建目录不再使用，确认无需保留后可自行删除。

### GitHub Actions

[构建工作流](.github/workflows/build.yml)同时支持发布构建和手动构建：

- 推送 `vX.Y.Z` Tag 时，工作流会验证 Tag 与 `CMakeLists.txt` 版本一致，构建并测试
  x64、Win32，从 `CHANGELOG.md` 提取对应版本章节作为 Release 说明，随后创建 GitHub
  Release，附带 `CryptoPrice-x64.zip`、`CryptoPrice-win32.zip` 和压缩包的 SHA-256 校验文件；
- 在 Actions 页面手动运行时，只构建并测试 x64、Win32，不创建 Release。两个 DLL 作为
  Workflow Artifacts（`CryptoPrice-x64`、`CryptoPrice-win32`）保留 14 天，可从该次运行页面下载。

所有压缩包和构建产物中的 DLL 均固定命名为 `CryptoPrice.dll`，不附加版本号、提交号或架构。

## 安装和使用

1. 完全退出 TrafficMonitor。
2. 将 `CryptoPrice.dll` 复制到 TrafficMonitor 的 `plugins` 文件夹。
3. 启动 TrafficMonitor，在插件管理中打开“Crypto 行情”的选项。
4. 选择 Bitget、OKX 或 Binance，再添加所需现货或合约标的。
5. 在 TrafficMonitor 显示设置中启用汇总项或各标的独立项。

数据源位于“标的独立设置”中，与交易对、市场类型和小数位数一起保存。选择列表中的标的，
修改数据源后点击“更新选中项”，再点“确定”。任务栏只显示标的和行情，汇总标签固定为“行情”；
交易所来源在设置列表、显示项名称及悬停提示中保留。
旧 INI 的 `[General] DataSource` 会迁移为每个标的的默认值，保存后写入各 `[InstrumentN] DataSource`。

输入 `BTC/USDT`、`BTC-USDT` 或 `btcusdt` 都会转换成内部 symbol `BTCUSDT`。现货界面显示 `BTC/USDT`，合约界面显示 `BTCUSDT`。

## 官方接口

- [Bybit V5 Tickers](https://bybit-exchange.github.io/docs/v5/market/tickers)
- [Kraken Futures Tickers](https://docs.kraken.com/api/docs/futures-api/trading/get-tickers/)
- [Kraken xStocks Ticker](https://docs.kraken.com/api/docs/rest-api/get-ticker-information/)（`asset_class=tokenized_asset`）
- [Bitget UTA Tickers](https://www.bitget.com/zh-CN/api-doc/uta/public/Tickers)
- [OKX REST Market Data](https://www.okx.com/docs-v5/en/#rest-api-market-data-get-tickers)
- [Binance Spot Market Data](https://developers.binance.com/docs/binance-spot-api-docs/rest-api/market-data-endpoints)
- [Binance Futures Market Data](https://developers.binance.com/docs/derivatives/usds-margined-futures/market-data/rest-api)

项目内的 `PluginInterface.h` 保持 TrafficMonitor 官方 API v7 虚函数顺序，以确保 DLL ABI 兼容。
