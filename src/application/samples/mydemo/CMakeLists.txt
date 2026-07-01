# src/application/samples/mydemo/CMakeLists.txt

if(DEFINED CONFIG_SAMPLE_SUPPORT_HELLOWORLD)
    add_subdirectory_if_exist(0_helloworld)
endif()

if(DEFINED CONFIG_SAMPLE_SUPPORT_THREAD)
    add_subdirectory_if_exist(1_thread)
endif()

if(DEFINED CONFIG_SAMPLE_SUPPORT_MUTEX)
    add_subdirectory_if_exist(2_mutex)
endif()

if(DEFINED CONFIG_SAMPLE_SUPPORT_QUEUE)
    add_subdirectory_if_exist(3_queue)
endif()

if(DEFINED CONFIG_SAMPLE_SUPPORT_EVENT)
    add_subdirectory_if_exist(4_event)
endif()

if(DEFINED CONFIG_SAMPLE_SUPPORT_TIMER)
    add_subdirectory_if_exist(5_timer)
endif()

if(DEFINED CONFIG_SAMPLE_SUPPORT_GPIO)
    add_subdirectory_if_exist(7_gpio)
endif()

if(DEFINED CONFIG_SAMPLE_SUPPORT_ADC)
    add_subdirectory_if_exist(8_adc)
endif()

if(DEFINED CONFIG_SAMPLE_SUPPORT_IIC)
    add_subdirectory_if_exist(10_iic)
endif()

if(DEFINED CONFIG_SAMPLE_SUPPORT_EMQX_TELEMETRY)
    add_subdirectory_if_exist(15_emqx_telemetry)
endif()

if(DEFINED CONFIG_SAMPLE_SUPPORT_AD8232_ADC)
    add_subdirectory_if_exist(9_ad8232_adc)
endif()

if(DEFINED CONFIG_SAMPLE_SUPPORT_SPO2_SENSOR AND NOT DEFINED CONFIG_SAMPLE_SUPPORT_AD8232_SLE_CLIENT)
    add_subdirectory_if_exist(14_spo2_sensor)
endif()

if(DEFINED CONFIG_SAMPLE_SUPPORT_WIFI)
    add_subdirectory_if_exist(11_wifi)
endif()

if(DEFINED CONFIG_SAMPLE_SUPPORT_AD8232_SLE)
    add_subdirectory_if_exist(13_ad8232_sle)
elseif(DEFINED CONFIG_SAMPLE_SUPPORT_SLE_UART OR DEFINED CONFIG_SAMPLE_SUPPORT_SLE)
    add_subdirectory_if_exist(12_sle)
endif()
        
# 关键：将下层收集的 SOURCES 变量传递给父级作用域
set(SOURCES "${SOURCES}" PARENT_SCOPE)
set(PUBLIC_HEADER "${PUBLIC_HEADER}" PARENT_SCOPE)

