# Crypto 行情插件

用于 [TrafficMonitor](https://github.com/zhongyang219/TrafficMonitor) 的多交易所现货与合约行情插件。可以在 Bitget、OKX、Binance 之间切换数据源，同时监控多个标的。

插件只调用交易所公共行情接口，不需要 API Key，也不会访问账户或执行交易。

## 项目文档

- [开发文档](docs/DEVELOPMENT.md)：架构、交易所适配、配置格式和构建方法
- [测试文档](docs/TESTING.md)：解析测试、DLL 烟雾测试和三家真实行情测试
- [部署文档](docs/DEPLOYMENT.md)：安装、从旧版升级、回滚和故障排查

## 功能

- 数据源支持 Bitget、OKX、Binance，可在选项窗口全局切换
- 同时配置最多 16 个现货或合约标的
- 支持现货、USDT 本位、USDC 本位和币本位合约
- “显示价格”可选择最新价格、标记价格或指数价格；该选项全局作用于合约，现货始终显示最新成交价
- 合约显示为 `BTCUSDT`，现货显示为 `BTC/USDT`
- 所有价格和 24 小时涨跌幅保留两位小数
- 默认任务栏项目汇总所有标的，同时提供各标的独立显示项
- 后台刷新行情，同一数据源和市场类别只请求一次
- 配置保存到 `CryptoPrice.ini`

## 统一市场映射

| 插件市场类型 | Bitget | OKX | Binance |
| --- | --- | --- | --- |
| 现货 | `SPOT` | `SPOT` | Spot |
| USDT 本位 | `USDT-FUTURES` | USDT Swap | USDⓈ-M Futures |
| USDC 本位 | `USDC-FUTURES` | USDC Swap | USDⓈ-M Futures |
| 币本位 | `COIN-FUTURES` | USD Swap | COIN-M Futures |

例如币本位 `BTCUSD` 会自动映射为 OKX 的 `BTC-USD-SWAP` 或 Binance 的 `BTCUSD_PERP`。

## 构建

需要 Visual Studio 2022（含“使用 C++ 的桌面开发”工作负载）和 CMake 3.20 或更高版本。

默认构建链路为 `CMake → Visual Studio 2022 / MSBuild → MSVC CL.exe`。CMake 负责生成
Visual Studio 工程，实际 C++ 编译由 MSVC 完成；当前不使用 Clang。

x64：

```powershell
cmake -S . -B build-x64 -A x64
cmake --build build-x64 --config Release
ctest --test-dir build-x64 -C Release --output-on-failure
```

Win32：

```powershell
cmake -S . -B build-win32 -A Win32
cmake --build build-win32 --config Release
ctest --test-dir build-win32 -C Release --output-on-failure
```

产物为 `build-x64/Release/CryptoPrice.dll` 或 `build-win32/Release/CryptoPrice.dll`。

### GitHub Actions

[构建工作流](.github/workflows/build.yml)同时支持发布构建和手动构建：

- 推送 `vX.Y.Z` Tag 时，工作流会验证 Tag 与 `CMakeLists.txt` 版本一致，构建并测试
  x64、Win32，从 `CHANGELOG.md` 提取对应版本章节作为 Release 说明，随后创建 GitHub
  Release，附带两个 DLL 和 SHA-256 校验文件；
- 在 Actions 页面手动运行时，只构建并测试 x64、Win32，不创建 Release。两个 DLL 作为
  Workflow Artifacts 保留 14 天，可从该次运行页面下载。

## 安装和使用

1. 完全退出 TrafficMonitor。
2. 将 `CryptoPrice.dll` 复制到 TrafficMonitor 的 `plugins` 文件夹。
3. 启动 TrafficMonitor，在插件管理中打开“Crypto 行情”的选项。
4. 选择 Bitget、OKX 或 Binance，再添加所需现货或合约标的。
5. 在 TrafficMonitor 显示设置中启用汇总项或各标的独立项。

输入 `BTC/USDT`、`BTC-USDT` 或 `btcusdt` 都会转换成内部 symbol `BTCUSDT`。现货界面显示 `BTC/USDT`，合约界面显示 `BTCUSDT`。

## 官方接口

- [Bitget UTA Tickers](https://www.bitget.com/zh-CN/api-doc/uta/public/Tickers)
- [OKX REST Market Data](https://www.okx.com/docs-v5/en/#rest-api-market-data-get-tickers)
- [Binance Spot Market Data](https://developers.binance.com/docs/binance-spot-api-docs/rest-api/market-data-endpoints)
- [Binance Futures Market Data](https://developers.binance.com/docs/derivatives/usds-margined-futures/market-data/rest-api)

项目内的 `PluginInterface.h` 保持 TrafficMonitor 官方 API v7 虚函数顺序，以确保 DLL ABI 兼容。
