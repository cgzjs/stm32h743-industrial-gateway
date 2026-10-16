cmake_minimum_required(VERSION 3.22)
project(Industrial_Gateway C ASM)
set(CMAKE_C_STANDARD 11)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CUBE ${CMAKE_CURRENT_SOURCE_DIR}/Vendor/STM32CubeH7)
set(HAL ${CUBE}/Drivers/STM32H7xx_HAL_Driver)
set(CMSIS ${CUBE}/Drivers/CMSIS)
set(RTOS ${CUBE}/Middlewares/Third_Party/FreeRTOS/Source)
set(LWIP ${CUBE}/Middlewares/Third_Party/LwIP/src)
set(FAT ${CUBE}/Middlewares/Third_Party/FatFs/src)
set(COMMON_INCLUDES Include Core/Inc BSP/bsp_board System/image_store System/data_model System/task_manager System/config_store Bootloader
    Middleware/crc Middleware/modbus_rtu Middleware/modbus_tcp Middleware/motor_can_protocol Middleware/firmware_transfer
    Application/ota_manager Application/hmi
    ${HAL}/Inc ${HAL}/Inc/Legacy ${CMSIS}/Include ${CMSIS}/Device/ST/STM32H7xx/Include)
set(PORTABLE Middleware/crc/igw_crc.c System/image_store/image_store.c Bootloader/boot_policy.c
    Application/ota_manager/ota_manager.c Middleware/firmware_transfer/fw_protocol.c
    System/data_model/data_model.c Middleware/modbus_rtu/modbus_rtu.c Middleware/modbus_tcp/modbus_tcp.c Middleware/motor_can_protocol/pd42.c)
set(HAL_SOURCES)
foreach(module hal hal_cortex hal_gpio hal_rcc hal_rcc_ex hal_pwr hal_pwr_ex hal_flash hal_flash_ex hal_dma hal_dma_ex hal_mdma hal_iwdg hal_qspi hal_uart hal_uart_ex hal_fdcan hal_eth hal_eth_ex hal_sd hal_sd_ex hal_sram ll_fmc ll_sdmmc)
    list(APPEND HAL_SOURCES ${HAL}/Src/stm32h7xx_${module}.c)
endforeach()
set(PLATFORM ${HAL_SOURCES} Core/Src/system_stm32h7xx.c Core/Src/stm32h7xx_it.c Core/Src/syscalls.c
    ${CMSIS}/Device/ST/STM32H7xx/Source/Templates/gcc/startup_stm32h743xx.s
    BSP/bsp_board/bsp_board.c BSP/bsp_flash/bsp_flash.c)
set(MCU_FLAGS -mcpu=cortex-m7 -mthumb -mfpu=fpv5-d16 -mfloat-abi=hard)
function(firmware target linker)
    target_include_directories(${target} PRIVATE ${COMMON_INCLUDES})
    target_compile_definitions(${target} PRIVATE USE_HAL_DRIVER STM32H743xx HSE_VALUE=25000000U)
    target_compile_options(${target} PRIVATE ${MCU_FLAGS} -ffunction-sections -fdata-sections -fstack-usage -Wall -Wextra -Wno-unused-parameter $<$<CONFIG:Debug>:-Og> $<$<CONFIG:Release>:-Os>)
    target_link_options(${target} PRIVATE ${MCU_FLAGS} -T${CMAKE_CURRENT_SOURCE_DIR}/${linker} --specs=nano.specs --specs=nosys.specs -Wl,--gc-sections -Wl,-Map=${target}.map -Wl,--print-memory-usage)
    target_link_libraries(${target} PRIVATE m c)
    set_target_properties(${target} PROPERTIES SUFFIX .elf LINK_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/${linker})
    add_custom_command(TARGET ${target} POST_BUILD
        COMMAND ${CMAKE_OBJCOPY} -O binary $<TARGET_FILE:${target}> ${target}.bin
        COMMAND ${CMAKE_OBJCOPY} -O ihex $<TARGET_FILE:${target}> ${target}.hex
        COMMAND ${CMAKE_SIZE} $<TARGET_FILE:${target}>)
endfunction()
add_executable(Bootloader ${PLATFORM} Middleware/crc/igw_crc.c System/image_store/image_store.c Bootloader/boot_policy.c Bootloader/main.c)
target_compile_definitions(Bootloader PRIVATE IGW_BOOTLOADER)
firmware(Bootloader Bootloader/STM32H743_BOOT.ld)
file(GLOB LVGL_SOURCES CONFIGURE_DEPENDS Vendor/lvgl/src/*.c)
file(GLOB_RECURSE LVGL_ALL CONFIGURE_DEPENDS Vendor/lvgl/src/*.c)
list(FILTER LVGL_ALL EXCLUDE REGEX "/extra/libs/qrcode/qrcodegen.c$")
file(GLOB LWIP_CORE ${LWIP}/core/*.c ${LWIP}/core/ipv4/*.c ${LWIP}/api/*.c)
set(RTOS_SOURCES ${RTOS}/tasks.c ${RTOS}/queue.c ${RTOS}/list.c ${RTOS}/timers.c ${RTOS}/event_groups.c ${RTOS}/stream_buffer.c
    ${RTOS}/portable/MemMang/heap_4.c ${RTOS}/portable/GCC/ARM_CM7/r0p1/port.c)
file(GLOB_RECURSE OUR_SOURCES CONFIGURE_DEPENDS Application/*.c System/*.c BSP/*.c)
list(REMOVE_ITEM OUR_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/BSP/bsp_board/bsp_board.c ${CMAKE_CURRENT_SOURCE_DIR}/BSP/bsp_flash/bsp_flash.c
    ${CMAKE_CURRENT_SOURCE_DIR}/System/image_store/image_store.c ${CMAKE_CURRENT_SOURCE_DIR}/System/data_model/data_model.c
    ${CMAKE_CURRENT_SOURCE_DIR}/Application/ota_manager/ota_manager.c)
add_executable(Gateway ${PLATFORM} ${PORTABLE} ${OUR_SOURCES} Core/Src/main.c Core/Src/image_identity.c ${RTOS_SOURCES} ${LWIP_CORE}
    ${LWIP}/netif/ethernet.c ${FAT}/ff.c ${LVGL_ALL})
target_include_directories(Gateway PRIVATE ${RTOS}/include ${RTOS}/portable/GCC/ARM_CM7/r0p1 ${LWIP}/include ${FAT} Vendor/lvgl)
target_compile_definitions(Gateway PRIVATE LV_CONF_INCLUDE_SIMPLE)
set(IGW_VERSION "0x00030000" CACHE STRING "Monotonically increasing firmware version")
target_compile_definitions(Gateway PRIVATE IGW_FW_VERSION=${IGW_VERSION}U)
option(IGW_TEST_BOOT_FAIL "Hang before the scheduler for rollback acceptance" OFF)
target_compile_definitions(Gateway PRIVATE $<$<BOOL:${IGW_TEST_BOOT_FAIL}>:IGW_TEST_BOOT_FAIL>)
firmware(Gateway Core/STM32H743_APP.ld)
