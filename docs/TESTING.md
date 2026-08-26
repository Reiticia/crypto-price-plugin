# 测试文档

## 1. 测试目标

测试覆盖：

- Bitget、OKX、Binance 响应字段差异；
- OKX 和 Binance 特殊 symbol 规范化；
- 三家涨跌幅统一成小数比例；
- 合约标记价、指数价和现货最新价；
- 数据源配置保存与读取；
- DLL 导出入口、API v7 和多标的显示项；
- 隐藏 24 小时涨跌幅后不再为百分比预留任务栏宽度；
- 新产物名称 `CryptoPrice.dll`。

## 2. 自动化测试

| 测试 | 联网 | 覆盖内容 |
| --- | --- | --- |
| `CryptoPriceTests` | 否 | 配置、显示格式及三家 JSON 解析 |
| `CryptoPriceSmoke` | 否 | DLL、API v7、混合现货/合约显示项 |
| `CryptoPriceNoChangeWidthSmoke` | 否 | 隐藏涨跌幅时汇总项和独立项的宽度示例不含百分比 |
| `CryptoPriceSmoke --live` | 是 | 指定数据源的真实合约和现货行情 |

默认 CTest 不执行联网测试，避免网络波动造成误报。

## 3. 构建与离线测试

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

三个离线测试都应显示 `Passed`。

## 4. 解析测试范围

`CryptoPriceTests` 验证：

- `BTC/USDT` 规范化成 `BTCUSDT`；
- 现货显示 `BTC/USDT`，合约显示 `BTCUSDT`；
- 数据源 key 的序列化和反序列化；
- Bitget ticker 与 API 错误码；
- OKX `BTC-USDT-SWAP` 转换、24 小时涨跌幅计算和标记价覆盖；
- Binance 百分数除以 100、premium index 覆盖；
- Binance `BTCUSD_PERP` 转换成 `BTCUSD`，并排除日期交割合约。

## 5. DLL 烟雾测试

```powershell
.\build-x64\Release\CryptoPriceSmoke.exe `
  .\build-x64\Release\CryptoPrice.dll `
  .\tests\fixtures
```

验证内容：

- 未修饰导出名 `TMPluginGetInstance`；
- `GetAPIVersion()` 返回 7；
- 汇总项包含 `BTCUSDT` 和 `ETH/USDT`；
- 两个独立显示项存在；
- 插件可以安全卸载。

隐藏涨跌幅后的宽度回归：

```powershell
.\build-x64\Release\CryptoPriceSmoke.exe `
  .\build-x64\Release\CryptoPrice.dll `
  .\tests\fixtures-no-change `
  --no-change
```

## 6. 三家真实行情测试

Bitget：

```powershell
.\build-x64\Release\CryptoPriceSmoke.exe `
  .\build-x64\Release\CryptoPrice.dll `
  .\tests\fixtures `
  --live
```

OKX：

```powershell
.\build-x64\Release\CryptoPriceSmoke.exe `
  .\build-x64\Release\CryptoPrice.dll `
  .\tests\fixtures-okx `
  --live
```

Binance：

```powershell
.\build-x64\Release\CryptoPriceSmoke.exe `
  .\build-x64\Release\CryptoPrice.dll `
  .\tests\fixtures-binance `
  --live
```

每组配置包含 BTCUSDT 合约和 ETH/USDT 现货，并选择合约标记价。通过条件：

- 两个标的均取得非 `--` 的实时价格；
- 现货在全局选择标记价时仍能显示最新成交价；
- 两个价格均保留两位小数；
- 进程退出码为 0。

实时测试最多等待约 20 秒，以覆盖 OKX 直连失败后依次尝试本地代理和 DoH 解析真实 IP 的首次请求。

## 7. DLL 导出和架构

```powershell
dumpbin /exports .\build-x64\Release\CryptoPrice.dll
```

或：

```powershell
llvm-readobj --coff-exports .\build-x64\Release\CryptoPrice.dll
```

必须包含：

```text
TMPluginGetInstance
```

同时确认 x64 产物为 `x86_64`，Win32 产物为 `i386`。

## 8. TrafficMonitor 手工验收

1. 安装 `CryptoPrice.dll`，插件管理应显示“Crypto 行情”2.0.0。
2. 配置 BTCUSDT 合约和 BTC/USDT 现货。
3. 依次切换 Bitget、OKX、Binance，保存后确认任务栏标签随数据源变化。
4. 确认合约显示 `BTCUSDT`，现货显示 `BTC/USDT`。
5. 切换最新价、标记价、指数价；现货必须始终有值。
6. 确认汇总项和独立项都能显示。
7. 重启 TrafficMonitor，确认数据源和标的配置被保留。
8. 断网后确认显示 `--`，tooltip 中包含交易所和网络错误。

## 9. 建议回归场景

- 每家数据源分别测试 0、1、2 和 16 个标的；
- SPOT、USDT、USDC、币本位混合；
- OKX `BTC-USD-SWAP` 与 Binance `BTCUSD_PERP`；
- OKX 直连失败时依次回退到区域入口、本地代理（Clash/v2rayN 常用端口）和 DoH 解析真实 IP；
- Binance 日期交割合约不会覆盖永续；
- 数据源连续快速切换，不能显示上一家的迟到响应；
- 2 秒和 300 秒刷新边界；
- 重复标的、非法 symbol、未知市场组合；
- 代理、防火墙、DNS、HTTP 非 200 和交易所错误码。

## 10. 常见失败

| 现象 | 排查方法 |
| --- | --- |
| CMake 找不到编译器 | 安装 Visual Studio C++ 桌面开发工作负载 |
| `LoadLibrary failed: 193` | DLL 与测试程序架构不一致 |
| OKX 价格为 `--` | 检查 `www.okx.com:443`、`eea.okx.com:443` 和 instrument 是否存在 |
| Binance 现货失败、合约正常 | 检查所在地区是否可访问 `api.binance.com` |
| 币本位找不到标的 | 使用插件标准 symbol，例如 `BTCUSD`，不要输入 `_PERP` 或 `-SWAP` |
| 实时测试超时 | 检查 DNS、代理、防火墙和交易所状态 |
