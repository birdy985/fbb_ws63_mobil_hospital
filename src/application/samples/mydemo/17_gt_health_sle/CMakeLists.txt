set(SOURCES_LIST
    ${CMAKE_CURRENT_SOURCE_DIR}/../13_ad8232_sle/sle_uart_client/sle_uart_client.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../13_ad8232_sle/ecg_sle_packet.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../16_gt_health/gt_health_sensor.c
    ${CMAKE_CURRENT_SOURCE_DIR}/main.c
)

set(HEADER_LIST
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/../13_ad8232_sle
    ${CMAKE_CURRENT_SOURCE_DIR}/../13_ad8232_sle/sle_uart_client
    ${CMAKE_CURRENT_SOURCE_DIR}/../16_gt_health
)

set(SOURCES "${SOURCES}" ${SOURCES_LIST} PARENT_SCOPE)
set(PUBLIC_HEADER "${PUBLIC_HEADER}" ${HEADER_LIST} PARENT_SCOPE)
