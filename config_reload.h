/*
 * config_reload.h -- 运行时配置重载支持
 */
#ifndef CONFIG_RELOAD_H
#define CONFIG_RELOAD_H

#include "dns.h"

#ifndef _WIN32
#include <signal.h>
extern volatile sig_atomic_t g_reload_requested;
#endif

/* 初始化重载模块的配置文件状态。 */
void runtime_reload_mark_loaded(const char *config_file);

/* 安装重载相关的运行时钩子（Linux下注册SIGHUP）。 */
void runtime_reload_setup(void);

/*
 * 强制重载配置文件。
 * reason用于日志输出，例如手动命令或信号触发。
 * 返回成功加载的记录数，失败返回-1。
 */
int runtime_reload_force(const char *config_file, FILE *log_file, const char *reason);

/*
 * 检测配置文件是否被修改；若修改则自动重载。
 * 需要在主循环中周期性调用。
 * 返回值：0表示未重载或重载成功，-1表示重载失败。
 */
int runtime_reload_poll(const char *config_file, FILE *log_file);

#endif /* CONFIG_RELOAD_H */