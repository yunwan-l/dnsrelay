# DNS Relay - 计算机网络课程设计

这是一个按照课程 PPT 要求整理过的 `DNS 中继服务器` 工程。  
和原来的单文件版本相比，现在已经拆成了多文件结构，方便分工、调试、写报告，也更接近老师验收时希望看到的工程组织方式。

## 1. 项目目标

程序实现以下 3 个核心功能：

1. 查询本地域名表，命中则直接返回对应 IP。
2. 若本地表中该域名对应 `0.0.0.0`，则返回 `NXDOMAIN`，实现拦截。
3. 若本地表未命中，则将查询转发给上游 DNS，并把结果返回客户端。

同时补上了老师在 PPT 中重点强调的两个问题：

1. 多客户端并发查询。
2. UDP 场景下的超时处理和迟到响应处理。

当前 `dnsrelay.txt` 采用的每行格式是：

```text
域名 IP地址
```

这和现在代码里的本地查表方式保持一致。

## 2. 当前工程结构

| 文件 | 作用 |
| --- | --- |
| `dnsrelay.c` | 主程序入口，负责参数解析、Socket 初始化和事件循环 |
| `dnsrelay.h` | 公共常量、平台兼容、调试宏 |
| `dns_protocol.c/.h` | DNS 报文头、问题段解析、响应报文构造 |
| `domain_table.c/.h` | 本地域名-IP 对照表加载、查找、打印 |
| `relay_engine.c/.h` | ID 映射、超时清理、上游转发、响应恢复 |
| `dnsrelay.txt` | 域名-IP 对照表 |
| `build_mingw.bat` | MinGW 一键编译脚本 |
| `Makefile` | 命令行构建脚本 |
| `docs/REPORT_OUTLINE.md` | 课程设计报告提纲 |
| `docs/TEST_CASES.md` | 建议测试用例 |
| `docs/ACCEPTANCE_CHECKLIST.md` | 验收前检查清单 |
| `docs/TEAM_DIVISION.md` | 3 人分工建议 |
| `docs/DEFENSE_QA.md` | 老师常问问题准备 |

## 3. 设计说明

### 3.1 为什么现在不是“只有一个 .c”

老师 PPT 里明确提到，不建议整个工程只有一个 `.c` 文件。  
现在已经按职责拆开：

- `主循环`
- `DNS 协议解析`
- `本地域名表`
- `中继与超时处理`

这样分工更清晰，也更利于答辩时每个人讲自己的模块。

### 3.2 为什么改成单 Socket

老师 PPT 里专门批评过“程序用了两个 socket，说明对 socket 机制理解不到位”。  
所以这里改成了：

- 一个 UDP socket 绑定本地 `53` 端口
- 同一个 socket 同时接收客户端查询和上游 DNS 的响应
- 通过 `来源地址 + DNS 头部 QR 位` 判断包的类型

这样结构更自然，也更符合课程设计的教学意图。

### 3.3 如何实现并发

不是多线程，而是 `select()` 事件驱动：

1. 收到客户端请求后，如果本地命中就直接响应。
2. 如果未命中，就分配一个新的中继 ID，把请求发给上游 DNS。
3. 然后立刻返回主循环，不阻塞等待。
4. 以后哪个响应先回来，就根据中继 ID 找到原客户端，再恢复原 ID 返回。

这才是真正满足老师 PPT 中“第一个查询尚未返回时还能继续处理第二个查询”的做法。

### 3.4 如何处理超时

程序维护一个 `TID 映射表`，每个中继请求都会记录：

- 上游使用的 ID
- 客户端原始 ID
- 客户端地址
- 原始查询报文
- 发送时间

如果超过 `TIMEOUT_SEC` 还没有上游响应，就：

1. 认为该查询超时。
2. 向客户端返回 `SERVFAIL`。
3. 删除映射条目。
4. 之后如果又收到迟到响应，则丢弃。

## 4. 编译

### MinGW

```bat
build_mingw.bat
```

或者：

```bat
gcc -Wall -Wextra -O2 dnsrelay.c dns_protocol.c domain_table.c relay_engine.c -lws2_32 -o dnsrelay.exe
```

### Visual Studio Developer Command Prompt

```bat
cl /W4 dnsrelay.c dns_protocol.c domain_table.c relay_engine.c ws2_32.lib
```

## 5. 运行

```bat
dnsrelay
dnsrelay -d
dnsrelay -dd
dnsrelay -d 8.8.8.8
dnsrelay -d 8.8.8.8 C:\dnsrelay.txt
```

参数语法与老师 PPT 保持一致：

```text
dnsrelay [ -d | -dd ] [ dns-server-ipaddr ] [ filename ]
```

## 6. 验收演示建议流程

1. 先 `ipconfig /all` 记下你电脑原来的 DNS。
2. 启动 `dnsrelay.exe`。
3. 把系统 DNS 改成 `127.0.0.1`。
4. 执行 `ipconfig /flushdns`。
5. 用 `nslookup`、`ping`、浏览器访问、Wireshark 抓包联合验证。

建议优先演示：

1. 本地命中。
2. 本地拦截。
3. 未命中转发。
4. 同时发起多个查询，说明 ID 转换机制。

## 7. 还需要你们自己做的事

代码只是基础，课程设计最终提交和验收还要看：

1. 报告写得是否完整。
2. 测试是否充分。
3. 每个成员是否都能讲清楚整体逻辑。
4. 能不能经得住老师现场追问和加功能。

所以请继续配合查看 `docs` 目录下的几个文件，把验收材料补齐。
