# DNS 中继服务器详细测试方案

本文档用于 `develop` 分支的验收测试。目标是让测试人员可以一步一步执行，并能根据终端输出判断功能是否通过。

## 1. 测试目的

通过本方案验证 DNS 中继服务器是否满足课程设计要求：

- 能正确读取本地域名表 `dnsrelay.txt`。
- 能处理本地命中查询，直接返回配置文件中的 IP。
- 能处理拦截查询，对 `0.0.0.0` 域名返回 `NXDOMAIN`。
- 能处理未命中查询，将请求转发给上游 DNS。
- 能支持多个查询连续或并发到达，不因为一个中继请求阻塞后续请求。
- 能完成 DNS ID 转换，保证上游响应能正确返回给原客户端。
- 能处理上游 DNS 超时，并返回 `SERVFAIL`。
- 能丢弃迟到或无法匹配的上游响应。
- 能在 `-d`、`-dd` 调试模式下输出便于验收的日志。

## 2. 测试环境

推荐环境：

- 操作系统：Windows
- 终端：VS Code 终端 PowerShell
- 编译器：MinGW GCC
- 分支：`develop`
- 运行方式：建议右键 VS Code，选择“以管理员身份运行”

注意：

- PowerShell 运行当前目录程序时要写 `.\dnsrelay.exe`，不能只写 `dnsrelay`。
- 本方案中的 `nslookup` 都显式指定 `127.0.0.1`，这样不需要先修改系统 DNS，也能证明请求经过本程序。
- 如果 `nslookup` 输出里的服务器地址不是 `127.0.0.1`，该次测试不能作为本程序测试结果。

## 3. 测试前准备

### 3.1 确认当前分支

目的：确保测试的是改造后的多文件版本，而不是旧的 `main` 单文件版本。

命令：

```powershell
git branch --show-current
git status
```

预期结果：

- 当前分支显示 `develop`。
- 如果不是 `develop`，执行：

```powershell
git switch develop
```

通过标准：

- 当前分支为 `develop`。

截图建议：

- 截图包含 `git branch --show-current` 的输出。

### 3.2 确认工程文件完整

目的：确认工程不是只有一个 `.c` 文件，符合老师 PPT 中“便于分工和模块化”的要求。

命令：

```powershell
dir
dir docs
```

预期结果：

- 根目录能看到 `dnsrelay.c`、`dns_protocol.c`、`domain_table.c`、`relay_engine.c` 等文件。
- `docs` 目录中能看到测试、报告、答辩、分工相关文档。

通过标准：

- 代码文件和文档文件齐全。

## 4. 编译测试

### 测试 1：一键脚本编译

目的：验证项目可以通过脚本正常编译，方便组员和老师复现。

命令：

```powershell
.\build_mingw.bat
```

预期结果：

- 编译成功。
- 当前目录生成 `dnsrelay.exe`。
- 没有 fatal error。

通过标准：

- `dnsrelay.exe` 存在，并且编译过程没有失败。

截图建议：

- 截图包含编译命令和成功输出。

### 测试 2：手动 GCC 编译

目的：证明多文件工程的编译命令正确，避免只编译 `dnsrelay.c` 导致链接失败。

命令：

```powershell
gcc -Wall -Wextra -O2 dnsrelay.c dns_protocol.c domain_table.c relay_engine.c -lws2_32 -o dnsrelay.exe
```

预期结果：

- 编译成功。
- 生成或覆盖 `dnsrelay.exe`。

通过标准：

- 没有 `undefined reference`、`fatal error` 等错误。

说明：

- `develop` 分支不能再用旧命令 `gcc dnsrelay.c -lws2_32 -o dnsrelay.exe`，因为代码已经拆成多个模块。

## 5. 启动与参数测试

### 测试 3：默认启动

目的：验证程序能绑定 UDP 53 端口并启动。

终端 1 执行：

```powershell
.\dnsrelay.exe
```

预期结果：

- 程序显示启动横幅。
- 能看到外部 DNS、配置文件、调试级别、本地域名数、监听端口等信息。
- 监听端口显示 `53`。

通过标准：

- 程序不退出。
- 没有出现“绑定端口 53 失败”。

如果失败：

- 关闭已经运行的 `dnsrelay.exe`。
- 用管理员身份重新打开 VS Code。
- 检查是否有其他程序占用了 53 端口。

### 测试 4：调试模式启动

目的：验证 `-d`、`-dd` 参数可以使用，并能输出测试所需日志。

终端 1 先按 `Ctrl + C` 结束旧程序，然后执行：

```powershell
.\dnsrelay.exe -d
```

观察后再按 `Ctrl + C`，执行：

```powershell
.\dnsrelay.exe -dd
```

预期结果：

- `-d` 启动时调试级别为 `1`。
- `-dd` 启动时调试级别为 `2`。
- `-dd` 会输出更多信息，例如本地域名表加载结果。

通过标准：

- 两种调试模式都能启动。
- 程序没有崩溃。

验收建议：

- 后续功能测试统一使用 `.\dnsrelay.exe -dd`，日志最完整。

### 测试 5：指定上游 DNS 启动

目的：验证命令行参数能指定外部 DNS 服务器。

终端 1 执行：

```powershell
.\dnsrelay.exe -dd 114.114.114.114
```

预期结果：

- 启动信息中的外部 DNS 显示 `114.114.114.114`。

通过标准：

- 程序正常启动。
- 后续中继查询能返回结果。

## 6. 基础功能测试

后续测试建议保持终端 1 运行：

```powershell
.\dnsrelay.exe -dd 114.114.114.114
```

另开终端 2 执行 `nslookup` 命令。

### 测试 6：本地命中测试

目的：验证本地表中存在的正常域名能直接返回配置文件中的 IP，不需要访问上游 DNS。

终端 2 执行：

```powershell
nslookup test1 127.0.0.1
nslookup test2 127.0.0.1
nslookup bupt 127.0.0.1
nslookup sina 127.0.0.1
nslookup sohu 127.0.0.1
```

预期结果：

- `test1` 返回 `11.111.11.111`。
- `test2` 返回 `22.22.222.222`。
- `bupt` 返回 `123.127.134.10`。
- `sina` 返回 `202.108.33.89`。
- `sohu` 返回 `61.135.181.175`。
- 终端 1 日志出现 `本地命中`。

通过标准：

- 返回 IP 与 `dnsrelay.txt` 中配置一致。
- `nslookup` 输出中的服务器地址为 `127.0.0.1`。

截图建议：

- 截图同时包含终端 2 查询结果和终端 1 的 `本地命中` 日志。

### 测试 7：本地拦截测试

目的：验证配置为 `0.0.0.0` 的域名会被拦截。

终端 2 执行：

```powershell
nslookup test0 127.0.0.1
nslookup www.xxx.com 127.0.0.1
nslookup www.pk.com 127.0.0.1
```

预期结果：

- 客户端显示查询失败、域名不存在或 `Non-existent domain`。
- 终端 1 日志出现 `本地拦截` 和 `NXDOMAIN`。

通过标准：

- 没有返回真实公网 IP。
- 程序没有崩溃。

注意：

- 如果查询 `www.xxx.com` 却返回了真实 IP，先检查 `nslookup` 是否指定了 `127.0.0.1`。

### 测试 8：未命中中继测试

目的：验证本地表中没有的域名会转发给上游 DNS。

终端 2 执行：

```powershell
nslookup www.baidu.com 127.0.0.1
nslookup www.qq.com 127.0.0.1
nslookup www.microsoft.com 127.0.0.1
```

预期结果：

- 客户端能收到上游 DNS 返回的 IP。
- 终端 1 日志出现 `中继查询`。
- `-dd` 模式下能看到 `中继返回成功`。

通过标准：

- 查询能返回公网 IP。
- 程序日志显示走的是中继路径，不是本地命中。

说明：

- 当前 `dnsrelay.txt` 中没有 `www.baidu.com`，所以它不是本地命中，而是中继测试域名。

### 测试 9：完整路径混合测试

目的：在一组命令中连续验证本地命中、拦截、中继三种路径。

终端 2 执行：

```powershell
nslookup bupt 127.0.0.1
nslookup www.xxx.com 127.0.0.1
nslookup www.baidu.com 127.0.0.1
```

预期结果：

- `bupt` 返回 `123.127.134.10`，日志为 `本地命中`。
- `www.xxx.com` 查询失败，日志为 `本地拦截`。
- `www.baidu.com` 返回公网 IP，日志为 `中继查询`。

通过标准：

- 三种处理路径都正确。
- 程序持续运行。

## 7. 压力与并发测试

### 测试 10：相同域名连续压力测试

目的：验证程序连续处理多个相同请求时不会崩溃。

终端 2 执行：

```powershell
for ($i=1; $i -le 20; $i++) { nslookup bupt 127.0.0.1 }
```

预期结果：

- 20 次都返回 `123.127.134.10`。
- 终端 1 连续出现查询日志。
- 程序不崩溃、不退出。

通过标准：

- 20 次查询全部有响应。

### 测试 11：混合压力测试

目的：验证本地命中、拦截、中继交替出现时程序仍然稳定。

终端 2 执行：

```powershell
for ($i=1; $i -le 5; $i++) {
    nslookup bupt 127.0.0.1
    nslookup www.xxx.com 127.0.0.1
    nslookup www.baidu.com 127.0.0.1
}
```

预期结果：

- 共 15 次查询。
- 本地命中、拦截、中继三类结果都正确。
- 程序不崩溃。

通过标准：

- 15 次查询全部得到合理响应。
- 终端 1 日志能看到三种路径交替出现。

### 测试 12：多终端并发测试

目的：验证程序不是阻塞式等待单个上游响应，而是能继续处理其他请求。

准备：

- 保持终端 1 运行 `.\dnsrelay.exe -dd 114.114.114.114`。
- 打开终端 2、终端 3、终端 4。

终端 2 执行：

```powershell
for ($i=1; $i -le 10; $i++) { nslookup www.baidu.com 127.0.0.1 }
```

终端 3 执行：

```powershell
for ($i=1; $i -le 10; $i++) { nslookup www.qq.com 127.0.0.1 }
```

终端 4 执行：

```powershell
for ($i=1; $i -le 10; $i++) { nslookup www.microsoft.com 127.0.0.1 }
```

预期结果：

- 多个终端都能陆续收到响应。
- 终端 1 日志连续出现 `中继查询`。
- `-dd` 模式下出现多条 `中继返回成功: upstream_id=... -> client_id=...`。

通过标准：

- 三个终端都能完成查询。
- 程序不中断。
- 没有明显卡死在第一条查询上。

说明：

- 这个测试用于向老师说明：程序使用 `select()` 和 ID 映射处理并发，而不是发出一个请求后阻塞等待。

## 8. ID 转换测试

### 测试 13：观察中继 ID 映射

目的：证明程序向上游 DNS 转发时会改写 ID，收到响应后再恢复客户端原 ID。

终端 1 启动：

```powershell
.\dnsrelay.exe -dd 114.114.114.114
```

终端 2 执行：

```powershell
nslookup www.baidu.com 127.0.0.1
nslookup www.qq.com 127.0.0.1
nslookup www.microsoft.com 127.0.0.1
```

预期结果：

- 终端 1 日志中每个查询都有客户端 `ID`。
- 中继返回时出现类似 `中继返回成功: upstream_id=... -> client_id=...` 的日志。

通过标准：

- 能看到 upstream_id 和 client_id 的对应关系。
- 查询结果返回给正确的 `nslookup` 命令。

截图建议：

- 截图重点保留 `ID`、`upstream_id`、`client_id` 相关日志。

## 9. 超时与异常测试

### 测试 14：上游 DNS 超时测试

目的：验证上游 DNS 不可达时，程序不会一直挂起，而是返回 `SERVFAIL` 并清理映射表。

终端 1 先结束旧程序，然后执行：

```powershell
.\dnsrelay.exe -dd 192.0.2.1
```

终端 2 执行：

```powershell
nslookup www.baidu.com 127.0.0.1
```

预期结果：

- 终端 1 日志出现 `中继查询`。
- 等待一段时间后出现 `上游DNS超时`。
- 客户端最终查询失败。
- 程序没有崩溃，仍可继续运行。

通过标准：

- 程序能检测超时。
- 客户端没有无限等待。
- 程序后续仍能接收新查询。

说明：

- `192.0.2.1` 是文档测试用保留地址，通常不可达，适合制造超时。

### 测试 15：超时后继续处理本地查询

目的：验证某个中继请求超时后，程序仍能处理新的本地命中和拦截请求。

保持终端 1 运行：

```powershell
.\dnsrelay.exe -dd 192.0.2.1
```

终端 2 在超时测试后继续执行：

```powershell
nslookup bupt 127.0.0.1
nslookup www.xxx.com 127.0.0.1
```

预期结果：

- `bupt` 仍能返回 `123.127.134.10`。
- `www.xxx.com` 仍能返回拦截结果。

通过标准：

- 上游不可达不影响本地命中和本地拦截。

### 测试 16：无效上游 DNS 参数测试

目的：验证程序能识别非法外部 DNS 地址。

命令：

```powershell
.\dnsrelay.exe -dd not_an_ip
```

预期结果：

- 程序输出 `无效的外部DNS地址`。
- 程序退出，不进入监听。

通过标准：

- 不崩溃。
- 不使用错误地址继续运行。

## 10. DNS 类型兼容测试

### 测试 17：AAAA 查询测试

目的：验证非 A 类型查询不会导致程序构造错误 A 记录或崩溃。

终端 1 启动：

```powershell
.\dnsrelay.exe -dd 114.114.114.114
```

终端 2 执行：

```powershell
nslookup -type=AAAA bupt 127.0.0.1
nslookup -type=AAAA www.baidu.com 127.0.0.1
```

预期结果：

- 查询不会导致程序崩溃。
- 对本地命中的非 A 类型查询，日志可能显示 `本地命中但类型非A/IN`。
- 对未命中的 AAAA 查询，可以走中继。

通过标准：

- 程序稳定运行。
- 不把 AAAA 查询错误地返回成本地 A 记录。

## 11. 配置文件测试

### 测试 18：自定义配置文件测试

目的：验证程序支持通过命令行指定域名表文件。

准备：

在当前目录新建 `test_dnsrelay.txt`，内容如下：

```text
demo.local 1.2.3.4
block.local 0.0.0.0
```

终端 1 执行：

```powershell
.\dnsrelay.exe -dd 114.114.114.114 test_dnsrelay.txt
```

终端 2 执行：

```powershell
nslookup demo.local 127.0.0.1
nslookup block.local 127.0.0.1
```

预期结果：

- `demo.local` 返回 `1.2.3.4`。
- `block.local` 返回拦截结果。
- 启动信息中的配置文件显示 `test_dnsrelay.txt`。

通过标准：

- 自定义配置文件生效。

测试后处理：

- 可以删除 `test_dnsrelay.txt`，避免误提交。

## 12. 可选验收展示测试

### 测试 19：系统 DNS 方式测试

目的：模拟真实使用场景，让系统 DNS 指向本程序。

步骤：

1. 先记录电脑原始 DNS 配置。
2. 将当前网络适配器 DNS 改为 `127.0.0.1`。
3. 终端 1 运行：

```powershell
.\dnsrelay.exe -dd 114.114.114.114
```

4. 终端 2 执行：

```powershell
ipconfig /flushdns
nslookup bupt
nslookup www.xxx.com
nslookup www.baidu.com
```

预期结果：

- 不显式写 `127.0.0.1` 时，`nslookup` 也会经过本程序。
- 三类查询结果与前面测试一致。

通过标准：

- `nslookup` 输出中的服务器地址为 `127.0.0.1` 或 `localhost`。

注意：

- 测试结束后一定要把系统 DNS 改回原来的地址或自动获取。

### 测试 20：Wireshark 抓包展示

目的：为报告和答辩准备更有说服力的证据。

建议抓包过滤条件：

```text
udp.port == 53
```

建议截图内容：

- 客户端向 `127.0.0.1:53` 发送查询。
- 程序向 `114.114.114.114:53` 转发未命中查询。
- 上游 DNS 返回响应。
- 程序返回给客户端。

通过标准：

- 能看出本程序处在客户端和上游 DNS 中间，完成中继。

## 13. 测试结果记录表

测试时可以按下面表格填写，后续写报告时直接引用。

| 编号 | 测试项 | 结果 | 截图/日志文件 | 备注 |
| --- | --- | --- | --- | --- |
| 1 | 一键脚本编译 | 通过 / 失败 |  |  |
| 2 | 手动 GCC 编译 | 通过 / 失败 |  |  |
| 3 | 默认启动 | 通过 / 失败 |  |  |
| 4 | 调试模式启动 | 通过 / 失败 |  |  |
| 5 | 指定上游 DNS | 通过 / 失败 |  |  |
| 6 | 本地命中 | 通过 / 失败 |  |  |
| 7 | 本地拦截 | 通过 / 失败 |  |  |
| 8 | 未命中中继 | 通过 / 失败 |  |  |
| 9 | 完整路径混合 | 通过 / 失败 |  |  |
| 10 | 相同域名压力 | 通过 / 失败 |  |  |
| 11 | 混合压力 | 通过 / 失败 |  |  |
| 12 | 多终端并发 | 通过 / 失败 |  |  |
| 13 | ID 转换 | 通过 / 失败 |  |  |
| 14 | 上游超时 | 通过 / 失败 |  |  |
| 15 | 超时后继续处理 | 通过 / 失败 |  |  |
| 16 | 无效上游参数 | 通过 / 失败 |  |  |
| 17 | AAAA 查询 | 通过 / 失败 |  |  |
| 18 | 自定义配置文件 | 通过 / 失败 |  |  |
| 19 | 系统 DNS 方式 | 通过 / 失败 |  | 可选 |
| 20 | Wireshark 抓包 | 通过 / 失败 |  | 可选 |

## 14. 常见问题判断

### 问题 1：PowerShell 提示找不到 `dnsrelay`

原因：

- PowerShell 默认不会从当前目录加载程序。

解决：

```powershell
.\dnsrelay.exe -dd
```

### 问题 2：`nslookup` 显示服务器不是 `127.0.0.1`

原因：

- 没有显式指定本地 DNS，或者系统 DNS 没有改成 `127.0.0.1`。

解决：

```powershell
nslookup 域名 127.0.0.1
```

### 问题 3：`for /L` 命令在 PowerShell 报错

原因：

- `for /L` 是 cmd 语法，不是 PowerShell 语法。

PowerShell 写法：

```powershell
for ($i=1; $i -le 20; $i++) { nslookup bupt 127.0.0.1 }
```

cmd 写法：

```bat
for /L %i in (1,1,20) do nslookup bupt 127.0.0.1
```

### 问题 4：绑定端口 53 失败

可能原因：

- 没有管理员权限。
- 已经有一个 `dnsrelay.exe` 在运行。
- 其他 DNS 服务占用了端口。

处理建议：

- 先按 `Ctrl + C` 结束旧程序。
- 以管理员身份重新打开 VS Code。
- 再执行 `.\dnsrelay.exe -dd`。

### 问题 5：中继查询失败，但本地命中和拦截正常

可能原因：

- 上游 DNS 不可达。
- 当前网络限制了 DNS 请求。
- 上游 DNS 参数写错。

处理建议：

```powershell
.\dnsrelay.exe -dd 114.114.114.114
```

然后重新测试：

```powershell
nslookup www.baidu.com 127.0.0.1
```

## 15. 验收时建议展示顺序

推荐按下面顺序展示给老师，最稳：

1. 展示工程结构，说明已经从单文件拆成多模块。
2. 编译项目，生成 `dnsrelay.exe`。
3. 用 `.\dnsrelay.exe -dd 114.114.114.114` 启动。
4. 演示本地命中：`nslookup bupt 127.0.0.1`。
5. 演示本地拦截：`nslookup www.xxx.com 127.0.0.1`。
6. 演示中继查询：`nslookup www.baidu.com 127.0.0.1`。
7. 演示混合压力测试。
8. 演示多终端并发测试。
9. 演示超时测试。
10. 结合日志解释 ID 转换、`select()`、超时清理。

