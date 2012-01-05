#ifndef TEST_SPAWN_H
#define TEST_SPAWN_H 1

typedef void (spawn_function)(void *);

void spawn_init(void);
void spawn_destroy(void);
void spawn(spawn_function *fn, void *ptr);

#endif
