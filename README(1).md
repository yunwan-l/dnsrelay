# DNS中继服务器 — 计算机网络课程设计

## 项目简介
实现一个运行在 Windows 上的 DNS 中继服务器，具备三大核心功能：
- **DNS服务器** — 查本地表，命中直接回复IP
- **网站拦截** — 表中IP为 0.0.0.0 时返回"域名不存在"
- **DNS中继** — 本地没查到，转发给外部DNS（如 202.106.0.20）

## 高级功能
- **多客户端并发** — 采用 select() 事件驱动，非忙等待，CPU占用≈0%
- **消息ID转换** — 中继时自动替换DNS报文ID，响应时恢复，区分不同客户端
- **超时处理 & 自动清理** — 外部DNS无响应时自动清理超时映射条目
- **域名指针压缩解析** — 支持RFC1035指针压缩，兼容标准DNS报文
- **调试信息分级** — -d 基本日志 / -dd 冗长日志，宏封装几乎不占CPU

## 技术栈
C语言 | Windows | UDP Socket | DNS协议(RFC 1035) | select多路复用

## 文件说明
| 文件 | 说明 |
|------|------|
| dnsrelay.c | 主程序源代码 |
| dnsrelay.txt | 域名-IP对照表（可自行编辑） |
| README.md | 本文件 |

## 编译
**MinGW：** `gcc dnsrelay.c -lws2_32 -o dnsrelay.exe`
**VS：** `cl dnsrelay.c wsock32.lib`

## 用法
```
dnsrelay                         # 默认：外部DNS 202.106.0.20，配置 dnsrelay.txt
dnsrelay -d                      # 调试模式（输出查询时间、序号、域名）
dnsrelay -dd                     # 详细调试模式（输出完整调试信息）
dnsrelay -d 8.8.8.8             # 指定外部DNS为8.8.8.8
dnsrelay -d 8.8.8.8 C:\a.txt    # 指定DNS和配置文件
```

## 运行步骤
1. 以管理员身份运行 dnsrelay.exe
2. 把电脑的DNS改成 127.0.0.1
3. 用浏览器上网或用 nslookup / ping 测试

## 测试命令
```
nslookup www.baidu.com          # 查询域名
ipconfig /displaydns            # 查看DNS缓存
ipconfig /flushdns              # 清除DNS缓存
ping www.baidu.com              # 测试连通性
```

## 配置文件（dnsrelay.txt）
```
# 正常域名
www.baidu.com 14.215.177.38
www.bupt.edu.cn 114.255.40.25

# 拦截域名
bad-site.com 0.0.0.0
```

## 三人分工
| 成员 | 负责模块 | 核心任务 |
|------|----------|----------|
| **A** | 主框架+Socket+中继 | main()、select多路复用、向外部DNS转发/接收、ID转换 |
| **B** | DNS协议解析+报文构造 | Header/QNAME/RR结构体、解析查询、构造响应、字节序 |
| **C** | 本地表+映射+测试 | 配置文件解析、域名查询、TidTable映射、超时清理、测试 |
