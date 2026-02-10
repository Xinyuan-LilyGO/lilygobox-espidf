#include "freertos/FreeRTOS.h"

extern "C" void app_main(void)
{
    printf("Ciallo\n");

    while (1)
    {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
