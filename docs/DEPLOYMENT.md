# 部署文档

## 1. 部署物

生产环境只需要：

```text
CryptoPrice.dll
```

不要部署测试程序、`.lib`、`.exp`、`.pdb`、源码或整个构建目录。

## 2. 网络要求

根据要使用的数据源放行 HTTPS 443：

| 数据源 | 域名 |
| --- | --- |
| Bitget | `api.bitget.com` |
| 美股（USD，Kraken xStocks） | `api.kraken.com` |
| OKX | `eea.okx.com`、`www.okx.com` |
| Binance 现货 | `api.binance.com` |
| Binance USDT/USDC 合约 | `fapi.binance.com` |
| Binance 币本位合约 | `dapi.binance.com` |

插件只访问公共行情接口，不需要配置 API Key、Secret 或 Passphrase。

美股选择“美股（USD）”，输入 `AAPL`、`TSLA` 等代码。该市场固定使用 Kraken xStocks
的美元最新价，不使用其他标的的数据源或全局合约价格类型，也不显示 24 小时涨跌幅。行情属于代币化美股，
不是证券交易所原生行情；USD 与 USDT 不混用。

## 3. 发布构建

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

工作流按架构生成发布压缩包：

```text
CryptoPrice-x64.zip
CryptoPrice-win32.zip
```

每个压缩包内只包含 `CryptoPrice.dll`，DLL 文件名不附加版本号、提交号或架构。
选择与 TrafficMonitor 架构一致的压缩包，解压后直接安装，无需重命名。Release 中的
`SHA256SUMS.txt` 记录两个压缩包的 SHA-256。手动发布前也可记录 DLL 哈希：

```powershell
Get-FileHash .\build\x64\Release\CryptoPrice.dll -Algorithm SHA256
```

## 4. 首次安装

1. 完全退出 TrafficMonitor。
2. 确认 TrafficMonitor 架构，选择对应的 DLL。
3. 将 `CryptoPrice.dll` 复制到 TrafficMonitor 的 `plugins` 文件夹。
4. 启动 TrafficMonitor。
5. 在“其他功能 → 插件管理”中确认“Crypto 行情”2.0.0 已加载。
6. 打开选项，选择 Bitget、OKX 或 Binance。
7. 添加现货或合约标的并保存。
8. 在 TrafficMonitor 显示设置中启用汇总项或独立项。

汇总示例：

```text
OKX: BTCUSDT 79800.10 +2.26% | ETH/USDT 3210.25 -0.14%
```

## 5. 显示项 ID

显示项 ID 为 `CryptoPrice0`（汇总项）和 `CryptoPriceItem0..15`（独立项）。TrafficMonitor
在“显示设置”中按 ID 保存勾选状态；更换 ID 后需要重新勾选要显示的项。插件只注册
当前已配置的标的，不显示空槽位。由于 TrafficMonitor 仅在启动时枚举插件显示项，运行期间
增删标的后需要重启 TrafficMonitor，才能同步“显示设置”中的项目数量。

## 6. 配置文件

新配置文件：

```text
CryptoPrice.ini
```

关键配置：

```ini
[General]
RefreshSeconds=5
ShowChange=1
PriceType=mark
Count=2

[Instrument0]
DataSource=binance
Category=USDT-FUTURES
Symbol=BTCUSDT
Precision=2

[Instrument1]
DataSource=okx
Category=SPOT
Symbol=ETHUSDT
Precision=-1
```

`DataSource` 与 `Precision` 都是每个标的独立配置：数据源为 `bitget`、`okx`、`binance`，
美股固定为 `kraken`；小数位数为 -1 自动、0–10 固定。旧版全局 `[General] DataSource`
在加载时作为缺少独立键的标的默认值，保存成功后自动迁移，无需手改 INI。

## 7. 部署后验证

- 插件管理显示“Crypto 行情”2.0.0；
- 每个标的使用自己设置的数据源，标签显示交易所；混合来源时汇总标签显示“多源”；
- 合约显示 `BTCUSDT`，现货显示 `BTC/USDT`；
- 最新价、标记价、指数价符合所选设置；
- 现货在选择标记价或指数价时仍显示最新价；
- 所有价格保留两位小数；
- 重启 TrafficMonitor 后配置仍有效；
- 任务栏没有因网络请求出现卡顿。

## 8. 回滚

1. 完全退出 TrafficMonitor。
2. 移除 `CryptoPrice.dll`。
3. 恢复备份的旧版 `CryptoPrice.dll`。
4. 启动 TrafficMonitor 并检查版本和行情。

## 9. 故障排查

| 现象 | 原因和处理 |
| --- | --- |
| 反复弹出“遇到不适当的参数” | TrafficMonitor 1.85.1 合并悬停提示后超过 MFC 的 1024 字符上限；升级到精简提示的插件版本。其他插件也可能占用该上限，仍超限时关闭主窗口和任务栏的鼠标提示，或减少其他插件提示长度 |
| 插件管理中没有插件 | 检查 DLL 路径及 x64/Win32 架构 |
| Windows 错误 193 | DLL 与 TrafficMonitor 架构不一致 |
| 切换数据源后仍显示旧标签 | 确认已点击“确定”，必要时重启 TrafficMonitor |
| 所有行情为 `--` | hover 查看错误，检查对应域名、防火墙和代理 |
| OKX 出现 Windows 错误 12007 | DNS 无法解析 OKX 入口；插件依次尝试 `eea.okx.com`、`www.okx.com`、本地代理（Clash/v2rayN 常用端口）和 DoH 解析真实 IP 直连，仍失败时检查 DNS、代理和两个域名 |
| 只有 Binance 现货失败 | 当前网络或地区可能无法访问 `api.binance.com` |
| 币本位标的找不到 | 使用 `BTCUSD`，插件会转换 `_PERP` 或 `-SWAP` |
| 配置无法保存 | 检查 TrafficMonitor 插件配置目录写权限 |
| 汇总太宽 | 减少汇总标的，或改用独立显示项布局 |

## 10. 安全与运维

- 不在 INI 中保存任何交易所密钥；
- 企业网络可按数据源最小化放行域名；
- 发布记录应包含版本、架构、构建时间和 SHA-256；
- 生产升级前应运行三家离线解析测试和实际使用数据源的 live 测试。
