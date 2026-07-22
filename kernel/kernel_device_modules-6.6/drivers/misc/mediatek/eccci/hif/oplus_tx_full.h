// oplus_tx_full.h
#ifndef OPLUS_TX_FULL_H
#define OPLUS_TX_FULL_H

#include <linux/types.h>

int oplus_tx_full_init(void);
void oplus_tx_full_fini(void);
void send_all_stats(u32 count, u32 queue, u32 index);


#endif // OPLUS_TX_FULL_H

