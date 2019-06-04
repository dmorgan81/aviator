#include <pebble.h>

#ifdef DEBUG
#define logv(fmt, args...) APP_LOG(APP_LOG_LEVEL_DEBUG_VERBOSE, fmt, args)
#define logd(fmt, args...) APP_LOG(APP_LOG_LEVEL_DEBUG, fmt, args)
#else
#define logv(fmt, args...)
#define logd(fmt, args...)
#endif

#define logi(fmt, args...) APP_LOG(APP_LOG_LEVEL_INFO, fmt, args)
#define logw(fmt, args...) APP_LOG(APP_LOG_LEVEL_WARNING, fmt, args)
#define loge(fmt, args...) APP_LOG(APP_LOG_LEVEL_ERROR, fmt, args)
