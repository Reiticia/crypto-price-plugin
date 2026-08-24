# 测试文档

## 1. 测试目标

测试覆盖以下风险：

- 交易对规范化和配置解析错误；
- Bitget JSON 字段或错误响应解析错误；
- DLL 入口、TrafficMonitor API v7 虚表不兼容；
- 多标的只出现在 tooltip、未出现在任务栏汇总项；
- 现货错误地读取不存在的标记价或指数价；
- 现货/合约交易对显示格式混淆；
- 实时价格没有统一保留两位小数；
- WinHTTP 无法访问真实 Bitget UTA 接口。

## 2. 自动化测试组成

| 测试 | 是否联网 | 覆盖内容 |
| --- | --- | --- |
| `BitgetPriceTests` | 否 | symbol 规范化、合法性校验、ticker 成功和错误响应解析 |
| `BitgetPriceSmoke` | 否 | 加载 DLL、API v7、合约与现货混合汇总项及独立项 |
| `BitgetPriceSmoke --live` | 是 | BTCUSDT 合约、ETH/USDT 现货实时行情及两位小数 |

默认 CTest 不运行联网测试，避免网络波动导致持续集成误报。

## 3. 构建并运行离线测试

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

通过标准：两个测试均显示 `Passed`，进程退出码为 0。

## 4. 单独运行测试

单元测试：

```powershell
.\build-x64\Release\BitgetPriceTests.exe
```

DLL 和混合市场烟雾测试：

```powershell
.\build-x64\Release\BitgetPriceSmoke.exe `
  .\build-x64\Release\BitgetPrice.dll `
  .\tests\fixtures
```

烟雾测试会加载 `tests/fixtures/BitgetPrice.ini`，并验证：

- `TMPluginGetInstance` 存在；
- `GetAPIVersion()` 返回 7；
- 第 0 项同时包含合约格式 `BTCUSDT` 和现货格式 `ETH/USDT`；
- 第 1、2 项是独立标的显示项；
- 第 3 项为空。

## 5. 真实行情测试

确认测试机能访问 `api.bitget.com:443`，然后执行：

```powershell
.\build-x64\Release\BitgetPriceSmoke.exe `
  .\build-x64\Release\BitgetPrice.dll `
  .\tests\fixtures `
  --live
```

测试最多等待约 10 秒，成功条件为：

- BTCUSDT 合约和 ETH/USDT 现货均取得非 `--` 的行情；
- 测试配置将全局价格类型设为 `mark`，现货仍能回退到最新成交价；
- 两个独立显示项的价格小数点后均为两位；
- 进程退出码为 0。

查询退出码：

```powershell
$LASTEXITCODE
```

## 6. DLL 导出检查

使用 Visual Studio：

```powershell
dumpbin /exports .\build-x64\Release\BitgetPrice.dll
```

或使用 LLVM：

```powershell
llvm-readobj --coff-exports .\build-x64\Release\BitgetPrice.dll
```

导出表必须包含未修饰的名称：

```text
TMPluginGetInstance
```

同时检查输出架构是 `x86_64` 或与目标 TrafficMonitor 对应的 `i386`。

## 7. TrafficMonitor 手工验收

1. 关闭 TrafficMonitor，安装待测 DLL 后重新启动。
2. 在插件管理中确认名称为“Bitget 行情”、版本为 1.2.0。
3. 打开插件选项，添加 BTCUSDT 合约、BTC/USDT 现货和 ETH/USDT 现货。
4. 确认默认任务栏项目直接显示三个标的，而不是只能 hover 查看。
5. 确认合约显示 `BTCUSDT`，现货显示 `BTC/USDT`。
6. 在 TrafficMonitor 显示设置中确认三个独立标的也可以分别启用。
7. 分别选择最新成交价、标记价格、指数价格，确认合约值变化、现货始终有最新价且均保留两位小数。
8. 关闭并重启 TrafficMonitor，确认配置仍然存在。
9. 输入错误类别或不存在的 symbol，确认显示 `--`，tooltip 给出错误原因。
10. 执行插件命令“立即刷新 Bitget 行情”，确认不会冻结任务栏。

## 8. 建议回归场景

- 0、1、2 和 16 个标的；
- 现货与三种合约类别混合配置；
- `BTC/USDT`、`BTC-USDT`、小写 symbol 的规范化；
- 显示或隐藏 24 小时涨跌幅；
- 2 秒和 300 秒刷新边界；
- 断网、DNS 失败、HTTP 非 200 和 Bitget 错误码；
- 同一个 BTCUSDT 同时配置为现货和合约，确认分别显示 `BTC/USDT`、`BTCUSDT`；
- 配置窗口连续添加、更新、删除及同市场重复标的拦截；
- 从 1.1.0 升级到 1.2.0 后，已有合约配置和显示项保持有效。

## 9. 常见测试失败

| 现象 | 排查方法 |
| --- | --- |
| CMake 找不到 C++ 编译器 | 安装 Visual Studio C++ 桌面开发工作负载 |
| `LoadLibrary failed: 193` | DLL 与测试程序架构不一致 |
| 离线烟雾测试找不到第三个显示项 | 确认传入的是包含测试 INI 的目录，而不是 INI 文件本身 |
| 实时测试超时 | 检查代理、防火墙、DNS 和 `api.bitget.com:443` |
| 价格为 `--` | 查看 tooltip，并核对 category 与 symbol 是否匹配 |
