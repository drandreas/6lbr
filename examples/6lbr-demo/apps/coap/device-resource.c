#include "coap-common.h"
#include "device-resource.h"

#if REST_RES_DEVICE_MODEL_SW
#define REST_RES_DEVICE_MODEL_SW_RESOURCE REST_RESOURCE
#else
#define REST_RES_DEVICE_MODEL_SW_RESOURCE(...)
#endif

#if REST_RES_DEVICE_UPTIME
#define REST_RES_DEVICE_UPTIME_RESOURCE REST_RESOURCE
#else
#define REST_RES_DEVICE_UPTIME_RESOURCE(...)
#endif

REST_RES_DEVICE_MODEL_SW_RESOURCE(device_model_sw,
    ,
    IF_RO_PARAMETER,
    DEVICE_MODEL_SW_RT,
    REST_FORMAT_ONE_STR("sw", CONTIKI_VERSION_STRING))

REST_RES_DEVICE_UPTIME_RESOURCE(device_uptime,
    ,
    IF_SENSOR,
    DEVICE_UPTIME_RT,
    REST_FORMAT_ONE_ULONG("uptime", clock_seconds()))