#ifndef _HIREDIS_FORMAT_H
#define _HIREDIS_FORMAT_H 1

int redis_format_vcommand(char **target, const char *format, va_list ap);
int redis_format_command(char **target, const char *format, ...);
int redis_format_command_argv(char **target, int argc, const char **argv, const size_t *argvlen);

#endif
