# Changelog

本项目所有重要变更都会记录在此文件中。格式参考 [Keep a Changelog](https://keepachangelog.com/zh-CN/1.1.0/)，版本号遵循[语义化版本](https://semver.org/lang/zh-CN/)。

## [Unreleased]

### 新增

- 价格小数位数可配置：留空由程序按行情自动推断，也可固定为 0–10 位

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
