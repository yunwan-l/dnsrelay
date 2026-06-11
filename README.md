# DNS Relay Server

计算机网络课程设计：DNS中继服务器。

## 编译

Windows/MinGW:

```bat
build.bat
```

等价命令：

```bat
gcc -Wall -Wextra -O2 -std=c99 -Wno-stringop-truncation -o dnsrelay.exe dns_relay.c dns_table.c tid_map.c dns_packet.c dns_cache.c stats.c upstream.c -lws2_32
```

Linux:

```sh
make linux
```

## 运行

```bat
dnsrelay.exe [-d | -dd] [dns-server-ipaddr] [filename]
```

示例：

```bat
dnsrelay.exe -d 8.8.8.8 dnsrelay.txt
dnsrelay.exe -dd 8.8.8.8,114.114.114.114 dnsrelay.txt
```

默认上游DNS为 `202.106.0.20`，默认配置文件为 `dnsrelay.txt`。

运行时支持热重载：

- 直接修改并保存配置文件，程序会在下一次轮询时自动加载新内容。
- 也可以在终端按 `r` 立即强制重载配置文件。
- Linux 下仍支持 `kill -HUP <pid>` 触发重载。

## 配置文件

推荐使用课程参考格式：

```text
IP地址 域名
```

示例：

```text
11.111.11.111 test1
0.0.0.0 008.cn
```

`0.0.0.0` 表示拦截该域名并返回 NXDOMAIN。程序也兼容旧格式 `域名 IP地址`。

## 快速测试

启动程序后，在另一个命令行窗口运行：

```bat
nslookup test1 127.0.0.1
nslookup 008.cn 127.0.0.1
nslookup www.bupt.edu.cn 127.0.0.1
```

并发测试可以运行：

```bat
test_t09.bat
```

## 文档

- `docs/课程设计报告.md`：按老师PDF要求整理的报告内容。
- `docs/功能清单.md`：区分课程基本功能和扩展功能。
- `docs/详细测试文档.md`：逐项测试步骤、预期输出和通过标准。
