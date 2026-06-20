#ifndef TANGPINGOS_USER_H
#define TANGPINGOS_USER_H

#include <limine.h>
#include <stdint.h>

void user_init_from_modules(struct limine_module_response *modules);
int user_spawn_from_vfs(const char *path);
int user_spawn_from_vfs_with_args(const char *path, const char *args);

#endif
