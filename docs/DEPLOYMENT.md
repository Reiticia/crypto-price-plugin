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
| OKX | `www.okx.com` |
| Binance 现货 | `api.binance.com` |
| Binance USDT/USDC 合约 | `fapi.binance.com` |
| Binance 币本位合约 | `dapi.binance.com` |

插件只访问公共行情接口，不需要配置 API Key、Secret 或 Passphrase。

## 3. 发布构建

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

建议发布包命名：

```text
CryptoPrice-2.0.0-x64.dll
CryptoPrice-2.0.0-win32.dll
```

安装时文件名使用 `CryptoPrice.dll`。发布前记录哈希：

```powershell
Get-FileHash .\build-x64\Release\CryptoPrice.dll -Algorithm SHA256
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

## 5. 从 BitgetPrice 1.x 升级

产物从 `BitgetPrice.dll` 更名为 `CryptoPrice.dll`。升级步骤：

1. 备份旧 `BitgetPrice.dll` 和 `BitgetPrice.ini`。
2. 完全退出 TrafficMonitor。
3. 从 `plugins` 目录移除旧 `BitgetPrice.dll`。
4. 复制新的 `CryptoPrice.dll`。
5. 启动 TrafficMonitor，确认只加载一个行情插件。
6. 新插件会在没有 `CryptoPrice.ini` 时读取旧 `BitgetPrice.ini`，数据源默认为 Bitget。
7. 打开选项并保存一次，生成新的 `CryptoPrice.ini`。

不要同时保留两个 DLL，否则 TrafficMonitor 会同时加载旧版和新版，出现重复显示项和重复网络请求。

显示项 ID `BitgetContractPrice0` 和 `BitgetContractPriceItem*` 被保留，因此原来的 TrafficMonitor 勾选状态通常可以继续使用。

## 6. 配置文件

新配置文件：

```text
CryptoPrice.ini
```

关键配置：

```ini
[General]
DataSource=binance
RefreshSeconds=5
ShowChange=1
PriceType=mark
Count=2
```

`DataSource` 支持 `bitget`、`okx`、`binance`。插件升级不会主动删除旧 INI，确认新版本稳定后可自行归档 `BitgetPrice.ini`。

## 7. 部署后验证

- 插件管理显示“Crypto 行情”2.0.0；
- 插件目录中不存在旧 `BitgetPrice.dll`；
- 切换数据源后汇总标签同步变化；
- 合约显示 `BTCUSDT`，现货显示 `BTC/USDT`；
- 最新价、标记价、指数价符合所选设置；
- 现货在选择标记价或指数价时仍显示最新价；
- 所有价格保留两位小数；
- 重启 TrafficMonitor 后配置仍有效；
- 任务栏没有因网络请求出现卡顿。

## 8. 回滚

1. 完全退出 TrafficMonitor。
2. 移除 `CryptoPrice.dll`。
3. 恢复备份的 `BitgetPrice.dll`。
4. 必要时恢复旧 `BitgetPrice.ini`。
5. 启动 TrafficMonitor 并检查版本和行情。

旧版不会读取 `CryptoPrice.ini`，所以回滚前不要删除旧配置备份。

## 9. 故障排查

| 现象 | 原因和处理 |
| --- | --- |
| 插件管理中没有插件 | 检查 DLL 路径及 x64/Win32 架构 |
| Windows 错误 193 | DLL 与 TrafficMonitor 架构不一致 |
| 出现两个行情插件 | 删除旧 `BitgetPrice.dll`，只保留 `CryptoPrice.dll` |
| 切换数据源后仍显示旧标签 | 确认已点击“确定”，必要时重启 TrafficMonitor |
| 所有行情为 `--` | hover 查看错误，检查对应域名、防火墙和代理 |
| 只有 Binance 现货失败 | 当前网络或地区可能无法访问 `api.binance.com` |
| 币本位标的找不到 | 使用 `BTCUSD`，插件会转换 `_PERP` 或 `-SWAP` |
| 配置无法保存 | 检查 TrafficMonitor 插件配置目录写权限 |
| 汇总太宽 | 减少汇总标的，或改用独立显示项布局 |

## 10. 安全与运维

- 不在 INI 中保存任何交易所密钥；
- 企业网络可按数据源最小化放行域名；
- 发布记录应包含版本、架构、构建时间和 SHA-256；
- 生产升级前应运行三家离线解析测试和实际使用数据源的 live 测试。
