# Changelog

本项目所有重要变更都会记录在此文件中。格式参考 [Keep a Changelog](https://keepachangelog.com/zh-CN/1.1.0/)，版本号遵循[语义化版本](https://semver.org/lang/zh-CN/)。

## [Unreleased]

### 新增

- 新增 Kraken 加密现货、USD 永续及 Bybit 现货、USDT/USDC/币本位永续数据源；按元数据映射实际交易对并排除交割合约，支持独立来源配置和价格校验
- 新增“美股（USD）”市场，使用 Kraken xStocks 美元最新价，支持股票代码输入、配置持久化及混合监控；美股不显示 24 小时涨跌幅
- 价格小数位数按标的独立配置（`Instrument.precision`）：留空由程序按行情自动推断，
  也可固定为 0–10 位；配置界面在选中标的时编辑小数位
- 配置格式调整：`[General] DisplayPrecision` 移除，改为每个 `[InstrumentN]` 段
  下的 `Precision` 键（缺省或脏值回退 -1 自动推断）

### 修复

- 限制并精简插件悬停提示，避免与外汇插件合并后超过 TrafficMonitor 1.85.1 的 1024 字符上限，反复弹出“遇到不适当的参数”

### 变更

- 使用统一的 CMake Presets，本地和 GitHub Actions 均输出至 `build/x64/Release/` 或 `build/win32/Release/`；构建最低要求为 CMake 3.21
- 数据源改为按标的独立设置，支持混合交易所及同名交易对；旧全局数据源自动迁移，查询按数据源与市场分组，设置列表及悬停提示保留来源，任务栏隐藏交易所名称
- 设置页区分标的独立设置与全局设置，缩短小数位数说明
- 仅向 TrafficMonitor 注册已配置的标的，不再在“显示设置”中显示未配置或预留槽位

## [2.0.1] - 2026-08-27

### 新增

- 行情请求失败时自动回退到备用 OKX 入口（`eea.okx.com`），避免 `www.okx.com` 解析挂起或返回错误地址
- 通过 DoH（DNS over HTTPS）解析服务器 IP，绕过被污染的本地 DNS
- 优先使用系统代理，并尝试常见本地代理端口（Clash / v2rayN 等）作为兜底
- 新增 `MarketClientProbe`、`MultiSourceProbe` 命令行探针工具

### 变更

- 源码文件和内部命名统一为 `CryptoPrice`，移除旧配置兼容

### 修复

- 删除并替换同一位置的标的后，任务栏仍显示旧交易对名称但价格已更新

## [2.0.0] - 2026-08-25

### 新增

- 支持 Bitget、OKX、Binance 三家交易所
- 新增 `MarketClient` 统一行情客户端，支持现货与合约（USDT 本位、币本位）市场
- 支持最新价与标记价格等价格类型
- 任务栏汇总项与各标的独立显示项
- 新配置格式 `CryptoPrice.ini`
- 新增 OKX、Binance、无变化宽度等烟雾测试夹具

### 变更

- 插件统一更名为 CryptoPrice

## [1.2.0] - 2026-08-24

### 新增

- 现货市场支持

## [1.1.0] - 2026-08-24

### 新增

- 首个版本：TrafficMonitor 插件，显示合约价格与 24 小时涨跌幅
