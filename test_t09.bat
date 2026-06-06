@echo off
chcp 65001 >nul

rem 默认启用的三组并发查询。
start "T09-bupt" cmd.exe /k "nslookup www.bupt.edu.cn 127.0.0.1"
start "T09-baidu" cmd.exe /k "nslookup www.baidu.com 127.0.0.1"
start "T09-qq" cmd.exe /k "nslookup www.qq.com 127.0.0.1"

rem 下面是备用的更多并发查询。
rem 需要增加并发数量时，删除某一行开头的 "rem " 即可启用。
rem start "T09-163" cmd.exe /k "nslookup www.163.com 127.0.0.1"
rem start "T09-sina" cmd.exe /k "nslookup www.sina.com.cn 127.0.0.1"
rem start "T09-sohu" cmd.exe /k "nslookup www.sohu.com 127.0.0.1"
rem start "T09-taobao" cmd.exe /k "nslookup www.taobao.com 127.0.0.1"
rem start "T09-jd" cmd.exe /k "nslookup www.jd.com 127.0.0.1"
rem start "T09-bilibili" cmd.exe /k "nslookup www.bilibili.com 127.0.0.1"
rem start "T09-zhihu" cmd.exe /k "nslookup www.zhihu.com 127.0.0.1"
rem start "T09-douyin" cmd.exe /k "nslookup www.douyin.com 127.0.0.1"

rem 下面是备用的本地域名命中和拦截测试。
rem start "T09-local-test1" cmd.exe /k "nslookup test1 127.0.0.1"
rem start "T09-local-bupt" cmd.exe /k "nslookup bupt 127.0.0.1"
rem start "T09-block-008" cmd.exe /k "nslookup 008.cn 127.0.0.1"
