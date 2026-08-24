# Bitget 行情插件

用于 [TrafficMonitor](https://github.com/zhongyang219/TrafficMonitor) 的 Bitget UTA 现货与合约行情插件。每个已配置标的会成为一个独立显示项，并提供任务栏汇总显示。

插件调用 Bitget 公共行情接口 `GET /api/v3/market/tickers`，不需要 API Key，也不会访问账户或交易数据。

## 项目文档

- [开发文档](docs/DEVELOPMENT.md)：架构、接口、配置格式和构建方法
- [测试文档](docs/TESTING.md)：单元测试、DLL 烟雾测试、实时行情测试和验收清单
- [部署文档](docs/DEPLOYMENT.md)：发布构建、安装、升级、回滚和故障排查

## 功能

- 默认任务栏显示项会在同一区域汇总最多 16 个现货或合约标的
- 同时提供每个标的的独立显示项，可在 TrafficMonitor 显示设置中按需启用
- 支持现货、USDT 本位、USDC 本位和币本位合约
- 合约可选择最新成交价、标记价格或指数价格；现货始终显示最新成交价
- 合约显示为 `BTCUSDT`，现货显示为 `BTC/USDT`
- 所有价格统一保留两位小数
- 可选显示 24 小时涨跌幅
- 刷新间隔可设为 2–300 秒
- 后台刷新行情；同一市场类别只发送一次请求
- 配置保存在 TrafficMonitor 插件配置目录的 `BitgetPrice.ini`

## 构建

需要 Visual Studio 2022（含“使用 C++ 的桌面开发”工作负载）和 CMake 3.20 或更高版本。

TrafficMonitor 和插件的架构必须一致。例如构建 64 位版本：

```powershell
cmake -S . -B build-x64 -A x64
cmake --build build-x64 --config Release
ctest --test-dir build-x64 -C Release --output-on-failure
```

构建 32 位版本：

```powershell
cmake -S . -B build-win32 -A Win32
cmake --build build-win32 --config Release
ctest --test-dir build-win32 -C Release --output-on-failure
```

生成的插件位于 `build-x64/Release/BitgetPrice.dll` 或 `build-win32/Release/BitgetPrice.dll`。

## 安装和使用

1. 关闭 TrafficMonitor。
2. 将 `BitgetPrice.dll` 复制到 TrafficMonitor 程序目录下的 `plugins` 文件夹。
3. 启动 TrafficMonitor，在“其他功能 → 插件管理”中确认“Bitget 行情”已加载。
4. 点击插件的“选项”，选择市场类型并输入交易对，通过“添加新标的”加入列表；可继续添加现货或合约。
5. 在 TrafficMonitor 的“显示设置”中勾选需要展示的 Bitget 行情显示项。

输入 `BTC/USDT`、`BTC-USDT` 或 `btcusdt` 都会自动转换为 API symbol `BTCUSDT`；界面会将现货显示成 `BTC/USDT`，合约显示成 `BTCUSDT`。如果价格显示为 `--`，将鼠标停在显示项上可查看错误原因；最常见原因是市场类型和交易对不匹配。

## 实现依据

- [TrafficMonitor 插件开发指南](https://github.com/zhongyang219/TrafficMonitor/wiki/%E6%8F%92%E4%BB%B6%E5%BC%80%E5%8F%91%E6%8C%87%E5%8D%97)
- [TrafficMonitor Stock 插件](https://github.com/zhongyang219/TrafficMonitorPlugins/tree/main/Plugins/Stock)
- [Bitget UTA 获取行情数据](https://www.bitget.com/zh-CN/api-doc/uta/public/Tickers)

项目内的 `PluginInterface.h` 保持 TrafficMonitor 官方 API v7 的虚函数顺序，以确保 DLL ABI 兼容。
