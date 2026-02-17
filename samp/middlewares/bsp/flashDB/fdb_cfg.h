/*
 * FlashDB configuration for samp project
 */

#ifndef FDB_CFG_H
#define FDB_CFG_H

/* FAL storage mode */
#define FDB_USING_FAL_MODE   1

/* Enable KVDB */
#define FDB_USING_KVDB       1

/* Enable TSDB */
#define FDB_USING_TSDB       1

/* Write granularity (bits): 32 for on-chip flash (4-byte aligned) */
#ifndef FDB_WRITE_GRAN
#define FDB_WRITE_GRAN       32
#endif

/* Disable debug output, keep error logs only */
#undef FDB_DEBUG_ENABLE
#define FDB_INFO_SILENT

#endif /* FDB_CFG_H */
