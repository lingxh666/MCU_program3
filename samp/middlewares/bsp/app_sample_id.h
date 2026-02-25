#ifndef __APP_SAMPLE_ID_H__
#define __APP_SAMPLE_ID_H__

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stddef.h>

/* 样本ID格式: YYYYMMDDHHmmss-SSS (18字节含'\0') */
#define SAMPLE_ID_BUF_SIZE  18

uint8_t  sample_id_init(void);
uint8_t  sample_id_generate(char *out_id, size_t buf_size);
uint16_t sample_id_get_current_seq(void);
void     sample_id_reset(void);

#ifdef __cplusplus
}
#endif

#endif /* __APP_SAMPLE_ID_H__ */
