# 部署文档

## 1. 部署物

生产部署只需要一个文件：

```text
BitgetPrice.dll
```

不要把测试程序、`.lib`、`.exp`、`.pdb` 或源码复制到 TrafficMonitor 的 `plugins` 目录。

## 2. 部署前检查

- TrafficMonitor 已完全退出；
- DLL 已按 Release 配置构建；
- DLL 架构与 TrafficMonitor 一致；
- 离线测试已通过；
- 发布前至少执行一次真实行情测试；
- 部署机器允许访问 `https://api.bitget.com:443`；
- 插件不需要 API Key、Secret 或 Passphrase。

可在任务管理器中确认 `TrafficMonitor.exe` 已退出。程序运行时直接覆盖 DLL 可能失败，或继续使用内存中的旧版本。

## 3. 生成 Release 包

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

建议发布文件命名：

```text
BitgetPrice-1.2.0-x64.dll
BitgetPrice-1.2.0-win32.dll
```

安装时仍应重命名为 `BitgetPrice.dll`。

建议同时记录 SHA-256：

```powershell
Get-FileHash .\build-x64\Release\BitgetPrice.dll -Algorithm SHA256
```

## 4. 首次安装

1. 完全退出 TrafficMonitor。
2. 找到 TrafficMonitor 主程序所在目录。
3. 如果不存在 `plugins` 文件夹，则创建它。
4. 将对应架构的 `BitgetPrice.dll` 复制到 `plugins` 文件夹。
5. 启动 TrafficMonitor。
6. 打开“其他功能 → 插件管理”，确认“Bitget 行情”加载成功。
7. 点击“选项”，添加所需现货或合约并保存。
8. 在 TrafficMonitor 显示设置中启用“Bitget 行情汇总”；需要单独布局时，再启用各标的独立显示项。

默认汇总显示格式示例：

```text
Bitget: BTCUSDT 77380.10 +0.26% | ETH/USDT 3210.25 -0.14%
```

## 5. 配置说明

配置由 TrafficMonitor 决定存放目录，文件名固定为：

```text
BitgetPrice.ini
```

插件升级不会主动删除或覆盖已有配置。建议部署前备份该文件。

配置窗口支持：

- 添加、更新和删除标的；
- 现货、USDT、USDC、币本位合约类别；
- 最新价、标记价、指数价；
- 是否显示 24 小时涨跌幅；
- 2–300 秒刷新间隔。

## 6. 从旧版本升级

1. 备份 `BitgetPrice.ini` 和旧版 DLL。
2. 完全退出 TrafficMonitor。
3. 用新版 `BitgetPrice.dll` 覆盖旧文件。
4. 重新启动 TrafficMonitor。
5. 在插件管理中确认版本为 1.2.0。
6. 检查已有合约配置是否保留，并确认新增现货显示为 `BTC/USDT`。

1.2.0 继续保留第一个显示项 ID `BitgetContractPrice0`，因此通常不需要重新勾选任务栏项目。各标的独立显示项按配置顺序提供，需要时可在显示设置中启用。

## 7. 部署后验证

- 插件管理显示版本 1.2.0；
- 任务栏汇总项直接显示所有现货与合约标的；
- 合约使用 `BTCUSDT` 格式，现货使用 `BTC/USDT` 格式；
- 价格均有两位小数；
- hover 能看到最新价、标记价、指数价及错误详情；
- “立即刷新 Bitget 行情”命令可用；
- 重启 TrafficMonitor 后配置仍然有效；
- 任务栏和 TrafficMonitor 主窗口无卡顿。

## 8. 回滚

1. 完全退出 TrafficMonitor。
2. 将当前 DLL 移出 `plugins` 目录。
3. 恢复备份的旧版 DLL。
4. 如旧版本不兼容新配置，再恢复对应的 `BitgetPrice.ini` 备份。
5. 启动 TrafficMonitor 并检查插件版本和显示项。

不要在 TrafficMonitor 运行时删除或替换 DLL。

## 9. 故障排查

| 现象 | 原因和处理 |
| --- | --- |
| 插件管理中没有插件 | 检查 DLL 是否在 `plugins` 目录，以及 DLL/TrafficMonitor 架构是否一致 |
| Windows 报错 193 | 使用了错误架构的 DLL |
| 仍显示旧版插件名称或行为 | 确认 TrafficMonitor 已完全退出并替换了正确 DLL；插件版本应为 1.2.0 |
| 汇总项未出现在任务栏 | 在 TrafficMonitor 显示设置中勾选“Bitget 行情汇总” |
| 只看到汇总、不见独立项 | 独立项需要在显示设置中分别启用，这是预期行为 |
| 行情显示 `--` | hover 查看错误；核对 symbol、category、网络和系统时间 |
| 所有行情请求失败 | 检查代理、防火墙和 `api.bitget.com:443` |
| 配置无法保存 | 检查 TrafficMonitor 插件配置目录的写权限 |
| 显示内容太宽 | 减少汇总标的数量，或改用各标的独立项进行布局 |

## 10. 安全与运维

- 插件只访问 Bitget 公共行情接口；
- 不读取账户信息，不执行交易；
- 不应在配置文件中保存任何 Bitget 密钥；
- 企业网络可仅放行 `api.bitget.com:443`；
- 建议发布时保留版本号、架构、构建时间和 SHA-256 记录。
