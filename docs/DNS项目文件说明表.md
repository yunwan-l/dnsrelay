# DNS中继服务器 - 项目文件说明

| 文件 | 作用 |
|------|------|
| **dns_relay.c** | 主程序入口，main函数，select事件循环，查询分发逻辑（判断命中/拦截/中继） |
| **dns.h** | 公共头文件，DNS协议结构体定义、跨平台宏（`#ifdef _WIN32`）、常量定义 |
| **dns_packet.c/.h** | DNS报文解析与构造，从二进制数据提取域名/QTYPE，构造响应报文 |
| **dns_table.c/.h** | 域名-IP对照表，从文件加载记录，支持精确匹配和通配符查找 |
| **tid_map.c/.h** | 事务ID映射表（并发中继核心），为每个转发请求分配新ID，响应回来时还原 |
| **dns_cache.c/.h** | DNS响应缓存，保存完整报文按TTL过期，缓存的key包含域名`\|`QTYPE`\|`QCLASS |
| **upstream.c/.h** | 上游DNS管理，支持多上游逗号分隔，超时自动切换，连接失败报错 |
| **stats.c/.h** | 运行统计，记录总查询/命中/拦截/中继/超时/错误数，按s键打印 |
| **config_reload.c/.h** | 热重载，检测配置文件修改时间自动重载、Linux SIGHUP信号处理、手动r键重载 |
| **build.bat** | Windows编译脚本（MinGW gcc） |
| **Makefile** | Linux编译脚本（gcc） |
| **dnsrelay.txt** | 默认域名配置文件（198条记录） |
| **format-test.txt** | 格式兼容性测试文件 |
| **test-table.txt** | 测试用域名表 |
| **test_t09.bat** | 并发测试批处理脚本 |
| **run.log** | 运行日志 |
| **docs/功能清单.md** | 功能说明文档 |
| **docs/课程设计报告.md** | 课设报告 |
| **docs/详细测试文档.md** | 测试文档 |
| **docs/知识点解析** | 知识点分析 |
| **docs/DNS日志含义对照表.md** | 日志标识含义说明 |
| **README.md** | 项目说明 |
| **references/** | RFC文档、参考资料、PPT等 |
