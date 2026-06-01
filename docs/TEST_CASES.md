# 建议测试用例

本文件是快速版测试清单。完整的一步步测试流程见：

```text
docs/DETAILED_TEST_PLAN.md
```

验收、截图、写报告时，建议以详细测试方案为准。

## 1. 测试前约定

- 测试分支：`develop`
- 推荐启动命令：`.\dnsrelay.exe -dd 114.114.114.114`
- 推荐客户端命令格式：`nslookup 域名 127.0.0.1`
- 如果 `nslookup` 输出里的服务器地址不是 `127.0.0.1`，该次结果不能证明程序通过测试。
- PowerShell 中运行当前目录程序要写 `.\dnsrelay.exe`，不能只写 `dnsrelay`。

## 2. 编译测试

```powershell
.\build_mingw.bat
```

或：

```powershell
gcc -Wall -Wextra -O2 dnsrelay.c dns_protocol.c domain_table.c relay_engine.c -lws2_32 -o dnsrelay.exe
```

预期：

- 能生成 `dnsrelay.exe`。
- 没有编译或链接错误。

## 3. 启动测试

```powershell
.\dnsrelay.exe -dd 114.114.114.114
```

预期：

- 程序显示启动信息。
- 外部 DNS 为 `114.114.114.114`。
- 配置文件为 `dnsrelay.txt`。
- 监听端口为 `53`。

## 4. 本地命中测试

```powershell
nslookup test1 127.0.0.1
nslookup test2 127.0.0.1
nslookup bupt 127.0.0.1
```

预期：

- `test1` 返回 `11.111.11.111`。
- `test2` 返回 `22.22.222.222`。
- `bupt` 返回 `123.127.134.10`。
- 服务端日志出现 `本地命中`。

## 5. 拦截测试

```powershell
nslookup test0 127.0.0.1
nslookup www.xxx.com 127.0.0.1
```

预期：

- 客户端显示查询失败或域名不存在。
- 服务端日志出现 `本地拦截` 和 `NXDOMAIN`。
- 不应返回真实公网 IP。

## 6. 中继测试

```powershell
nslookup www.baidu.com 127.0.0.1
nslookup www.qq.com 127.0.0.1
nslookup www.microsoft.com 127.0.0.1
```

预期：

- 客户端能收到公网 IP。
- 服务端日志出现 `中继查询`。
- `-dd` 模式下能看到 `中继返回成功`。

## 7. 混合压力测试

```powershell
for ($i=1; $i -le 5; $i++) {
    nslookup bupt 127.0.0.1
    nslookup www.xxx.com 127.0.0.1
    nslookup www.baidu.com 127.0.0.1
}
```

预期：

- 共 15 次查询。
- 本地命中、拦截、中继三种路径都正确。
- 程序不崩溃。

## 8. 并发测试

打开多个 VS Code 终端，同时执行：

```powershell
for ($i=1; $i -le 10; $i++) { nslookup www.baidu.com 127.0.0.1 }
```

```powershell
for ($i=1; $i -le 10; $i++) { nslookup www.qq.com 127.0.0.1 }
```

```powershell
for ($i=1; $i -le 10; $i++) { nslookup www.microsoft.com 127.0.0.1 }
```

预期：

- 多个终端都能陆续得到响应。
- 服务端不中断、不崩溃。
- 日志能看到多次 `中继查询` 和 `中继返回成功`。

## 9. 超时测试

服务端启动：

```powershell
.\dnsrelay.exe -dd 192.0.2.1
```

客户端测试：

```powershell
nslookup www.baidu.com 127.0.0.1
```

预期：

- 服务端日志出现 `上游DNS超时`。
- 客户端最终查询失败或收到 `SERVFAIL`。
- 程序不崩溃。

## 10. 非 A 类型测试

```powershell
nslookup -type=AAAA bupt 127.0.0.1
nslookup -type=AAAA www.baidu.com 127.0.0.1
```

预期：

- 程序不崩溃。
- 本地命中的非 A 查询不会被错误构造成 A 记录。

## 11. 结果记录

建议每类测试至少保存一张截图：

- 编译成功截图。
- 程序启动截图。
- 本地命中截图。
- 拦截截图。
- 中继截图。
- 并发或压力测试截图。
- 超时测试截图。

更完整的记录表见 `docs/DETAILED_TEST_PLAN.md`。

