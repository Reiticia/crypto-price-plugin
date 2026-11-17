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
| `CryptoPriceTests` | 否 | 配置、显示格式、三家 JSON 解析及标的价格有效性校验 |
| `CryptoPriceSmoke` | 否 | DLL、API v7、混合现货/合约显示项 |
| `CryptoPriceNoChangeWidthSmoke` | 否 | 隐藏涨跌幅时汇总项和独立项的宽度示例不含百分比 |
| `CryptoPriceStreamTests` | 否 | 五家 WSS 订阅、Bybit 快照／delta、频道合并、错误、重连空缓存、股票映射、缺失快照告警去重 |
| `CryptoPriceStreamLifecycleTests` | 否 | 无网络场景的停止、旧回调隔离、配置代次切换、析构与关闭状态 |
| `CryptoPriceStreamDisplayTests` | 否 | 混合成功／失败标的的 300 秒节流、错误即时清价、待显示报价保留、generation 隔离 |
| `CryptoPriceStreamSocketTests` | 仅本机回环 | 真实 WinHTTP 阻塞接收取消、关闭握手超时、重复取消和取消后的收发保护 |
| `CryptoPriceExchangeTests` | 否 | Kraken 现货元数据与数字型永续、Bybit 结算币／交割过滤、API 错误和分页解析 |
| `CryptoPriceOptionsSourceTests` | 否 | 数据源位于独立区、选中加载各自来源、美股锁定 Kraken、取消不提交 |
| `CryptoPriceMixedSourceSmoke` | 否 | 混合 Binance/OKX 来源正确，任务栏标签、值与示例不含交易所，汇总标签为“行情” |
| `CryptoPriceStockSmoke` | 否 | 美股 USD 标签、Kraken 数据源名称、无涨跌幅占位 |
| `CryptoPriceSmoke --live` | 是 | 指定数据源的真实合约和现货行情 |

默认 CTest 不访问外部网络，避免网络波动造成误报；Socket 测试仅使用本机回环临时端口。

## 3. 构建与离线测试

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

十一个离线测试都应显示 `Passed`。

## 4. 解析测试范围

`CryptoPriceTests` 验证：

- `BTC/USDT` 规范化成 `BTCUSDT`；
- 现货显示 `BTC/USDT`，合约显示 `BTCUSDT`；
- 数据源 key 的序列化和反序列化；独立来源配置往返、旧全局数据源迁移、独立键优先、保存后清除旧键；
- 按（数据源、市场）缓存校验报价，禁止将另一交易所的报价用于缺失标的；同 symbol 不同来源可并存；
- 美股短代码与 USD/xUSD 输入规范化、`US-STOCK` 配置往返和最新价校验；
- Kraken xStocks 仅接收 `xUSD`，排除 USDT、EUR 和内部 SPV 别名，拒绝错误／残缺响应；
- 美股不将当日涨幅冒充 24 小时涨幅；
- 悬停提示不超过 384 个 UTF-16 单元，超长标题、16 标的、错误文字均截断，并为宿主及其他插件预留空间；
- Bitget ticker 与 API 错误码；
- OKX `BTC-USDT-SWAP` 转换、24 小时涨跌幅计算和标记价覆盖；
- Binance 百分数除以 100、premium index 覆盖；
- Binance `BTCUSD_PERP` 转换成 `BTCUSD`，并排除日期交割合约。

## 5. DLL 烟雾测试

```powershell
.\build\x64\Release\CryptoPriceSmoke.exe `
  .\build\x64\Release\CryptoPrice.dll `
  .\tests\fixtures
```

验证内容：

- 未修饰导出名 `TMPluginGetInstance`；
- `GetAPIVersion()` 返回 7；
- 汇总项包含 `BTCUSDT` 和 `ETH/USDT`；
- 已配置的两个独立显示项存在；
- 已配置项之后不注册空槽位；
- 未启动网络的 ABI 检查不发请求，插件可释放；启动 WebSocket 后 DLL 保持映射，只支持退出宿主后替换。

隐藏涨跌幅后的宽度回归：

```powershell
.\build\x64\Release\CryptoPriceSmoke.exe `
  .\build\x64\Release\CryptoPrice.dll `
  .\tests\fixtures-no-change `
  --no-change
```

美股离线显示及实际联网测试（仅查询公共接口，不下单）：

```powershell
.\build\x64\Release\CryptoPriceSmoke.exe `
  .\build\x64\Release\CryptoPrice.dll `
  .\tests\fixtures-stocks --stocks --live
```

应显示 `AAPL/USD`、`TSLA/USD` 的最新价，来源为 Kraken xStocks；即使配置为标记价，
美股仍使用美元最新价，不显示 24 小时百分比。

## 6. 三家真实行情测试

Bitget：

```powershell
.\build\x64\Release\CryptoPriceSmoke.exe `
  .\build\x64\Release\CryptoPrice.dll `
  .\tests\fixtures `
  --live
```

OKX：

```powershell
.\build\x64\Release\CryptoPriceSmoke.exe `
  .\build\x64\Release\CryptoPrice.dll `
  .\tests\fixtures-okx `
  --live
```

Binance：

```powershell
.\build\x64\Release\CryptoPriceSmoke.exe `
  .\build\x64\Release\CryptoPrice.dll `
  .\tests\fixtures-binance `
  --live
```

每组配置包含 BTCUSDT 合约和 ETH/USDT 现货，并选择合约标记价。通过条件：

- 两个标的均取得非 `--` 的实时价格；
- 现货在全局选择标记价时仍能显示最新成交价；
- 两个价格均保留两位小数；
- 进程退出码为 0。

实时测试最多等待约 20 秒，以覆盖 OKX 直连失败后依次尝试本地代理和 DoH 解析真实 IP 的首次请求。

### Kraken / Bybit 联网回归（显式执行）

```powershell
.\build\x64\Release\CryptoPriceExchangeLiveSmoke.exe kraken SPOT BTCUSD last
.\build\x64\Release\CryptoPriceExchangeLiveSmoke.exe kraken USD-FUTURES BTCUSD mark
.\build\x64\Release\CryptoPriceExchangeLiveSmoke.exe bybit SPOT BTCUSDT last
.\build\x64\Release\CryptoPriceExchangeLiveSmoke.exe bybit USDT-FUTURES BTCUSDT mark
.\build\x64\Release\CryptoPriceExchangeLiveSmoke.exe bybit USDC-FUTURES BTCUSDC index
.\build\x64\Release\CryptoPriceExchangeLiveSmoke.exe bybit COIN-FUTURES BTCUSD last
```

上面的 ExchangeLiveSmoke 是显式 REST 诊断，不是插件运行路径；Kraken REST 现货涨跌幅为空。
运行时 WebSocket v2 则使用真实的 change_pct。分别检查最新价、标记价、指数价与结算币。
Bybit 若返回 HTTP 403，先检查地区／网络限制，不应绕过限制或回退成其他交易所的价格。

### WebSocket 实际推送回归

```powershell
.\build\x64\Release\CryptoPriceStreamLiveSmoke.exe bitget USDT-FUTURES BTCUSDT
.\build\x64\Release\CryptoPriceStreamLiveSmoke.exe okx USDT-FUTURES BTCUSDT
.\build\x64\Release\CryptoPriceStreamLiveSmoke.exe binance USDT-FUTURES BTCUSDT
.\build\x64\Release\CryptoPriceStreamLiveSmoke.exe bybit USDC-FUTURES BTCUSDC
.\build\x64\Release\CryptoPriceStreamLiveSmoke.exe kraken SPOT BTCUSD
.\build\x64\Release\CryptoPriceStreamLiveSmoke.exe kraken USD-FUTURES BTCUSD
.\build\x64\Release\CryptoPriceStreamLiveSmoke.exe kraken US-STOCK AAPL
```

最多等待 35 秒，必须收到至少一条有效价格，活跃市场通常两条以上。追加 `--hold` 会等待\n18 秒之后的新报价，覆盖应用心跳周期；追加 `--reconnect` 会主动停止后以新 generation\n重连，要求新 session 取到报价且旧 session 在 Stop 之后不再回调。
另用代理／防火墙模拟断网、恢复后重连，检查旧价被清空且自动重订阅。显示间隔设为
300 秒时，断线提示仍应及时出现；修改标的后旧 session 不得覆盖新配置。
REST 只应出现在必要元数据和主动设置校验，不能周期请求 ticker。

## 7. DLL 导出和架构

```powershell
dumpbin /exports .\build\x64\Release\CryptoPrice.dll
```

或：

```powershell
llvm-readobj --coff-exports .\build\x64\Release\CryptoPrice.dll
```

必须包含：

```text
TMPluginGetInstance
```

同时确认 x64 产物为 `x86_64`，Win32 产物为 `i386`。

## 8. TrafficMonitor 手工验收

1. 安装 `CryptoPrice.dll`，插件管理应显示“Crypto 行情”2.0.0。
2. 配置 BTCUSDT 合约和 BTC/USDT 现货。
3. 分别为标的选择 Bitget、OKX、Binance，点“更新选中项”后保存，确认各自标签和行情来源正确。同一交易对可添加不同来源，切换列表选择时数据源跟随标的。
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
- 2 秒和 300 秒显示间隔边界，推送接收不断开、不触发 REST 轮询；
- 重复标的、非法 symbol、未知市场组合；
- 添加不存在的标的时弹出失败原因，列表和配置不变；更新为不存在的标的时保留原项；
- 添加有效标的后切换数据源或价格类型，点击确定会重新校验整个列表，失败时不保存；
- 所选价格为空、非数字、非有限数或非正数时拒绝；现货在标记价/指数价模式下仍校验最新价；
- 断网时禁止添加、更新和保存非空列表，恢复网络后可重试；空列表无需联网即可保存；
- 校验中禁止重复提交和修改查询参数；取消后不提交结果，等待后台查询结束再关闭；
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
