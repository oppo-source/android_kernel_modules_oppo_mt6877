#ifndef __AOLEXTRA_CORE_H__
#define __AOLEXTRA_CORE_H__
#define MAX_LOG_SIZE 512

#include <linux/types.h>
#include <linux/compiler.h>

int aolextra_core_init(void);
void aolextra_core_deinit(void);

#endif //__AOLEXTRA_CORE_H__