// 替换协议处理逻辑的脚本内容
sed -i '/case PROTOCOL_GUOBIAO:/,/respLen = 0;/c\
            case PROTOCOL_GUOBIAO:   // 国标协议（未实现，使用大岳协议）\
                printf("[任务7] 配置为国标协议，使用大岳协议处理\r\n");\
                respLen = handle_dayue_protocol(&message, response);\
                break;' freertos_app.c

sed -i '/case PROTOCOL_XIAN:/,/respLen = 0;/c\
            case PROTOCOL_XIAN:      // 西安协议（已移至UART7，这里使用大岳协议）\
                printf("[任务7] 配置为西安协议，使用大岳协议处理（建议使用UART7）\r\n");\
                respLen = handle_dayue_protocol(&message, response);\
                break;' freertos_app.c

sed -i '/case PROTOCOL_NANJING:/,/respLen = 0;/c\
            case PROTOCOL_NANJING:   // 南京协议（未实现，使用大岳协议）\
                printf("[任务7] 配置为南京协议，使用大岳协议处理\r\n");\
                respLen = handle_dayue_protocol(&message, response);\
                break;' freertos_app.c

sed -i '/default:/,/respLen = 0;/c\
            default:                // 未知协议，也使用大岳协议\
                printf("[任务7] 未知协议配置=%d，使用大岳协议处理\r\n",\
                       g_CommSettingConfig.Protocol);\
                respLen = handle_dayue_protocol(&message, response);\
                break;' freertos_app.c

sed -i 's/case PROTOCOL_DAYUE: \/\/ 大岳协议/case PROTOCOL_DAYUE:     \/\/ 大岳协议/' freertos_app.c
sed -i '/case PROTOCOL_DAYUE:/a\                printf("[任务7] 使用大岳协议处理\r\n");' freertos_app.c

sed -i '/\/\* case PROTOCOL_DAYUE: \*\/a\          // 注意：由于国标协议和南京协议未实现，西安协议已移至UART7\n          // 现统一使用大岳协议处理所有请求，确保系统能正常响应' freertos_app.c
