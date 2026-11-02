
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"

#include "driver/rmt_rx.h"
#include "esp_log.h"
#include "esp_err.h"

#define IR_RX_GPIO             4
#define IR_RESOLUTION_HZ       1000000
#define IR_BUFFER_SYMBOLS      512

static const char *TAG = "TESTE_IR";

static rmt_channel_handle_t rx_channel = NULL;

static QueueHandle_t receive_queue = NULL;

static rmt_symbol_word_t raw_symbols[IR_BUFFER_SYMBOLS];

static const rmt_receive_config_t receive_config = {
    .signal_range_min_ns = 1000,
    .signal_range_max_ns = 30000000
};

// Callback executado quando uma captura termina
static bool ir_receive_callback(
    rmt_channel_handle_t channel,
    const rmt_rx_done_event_data_t *edata,
    void *user_data
) {
    BaseType_t higher_priority_task_woken = pdFALSE;

    xQueueSendFromISR(
        receive_queue,
        edata,
        &higher_priority_task_woken
    );

    return higher_priority_task_woken == pdTRUE;
}

// Inicializa o receptor infravermelho
static void ir_receiver_init(void) {

    rmt_rx_channel_config_t rx_config = {
        .gpio_num = IR_RX_GPIO,
        .clk_src = RMT_CLK_SRC_DEFAULT,
        .resolution_hz = IR_RESOLUTION_HZ,
        .mem_block_symbols = 64,
        .flags.invert_in = false,
        .flags.with_dma = false
    };

    ESP_ERROR_CHECK(
        rmt_new_rx_channel(&rx_config, &rx_channel)
    );

    receive_queue = xQueueCreate(
        1,
        sizeof(rmt_rx_done_event_data_t)
    );

    if (receive_queue == NULL) {
        ESP_LOGE(TAG, "Erro ao criar fila");
        abort();
    }

    rmt_rx_event_callbacks_t callbacks = {
        .on_recv_done = ir_receive_callback
    };

    ESP_ERROR_CHECK(
        rmt_rx_register_event_callbacks(
            rx_channel,
            &callbacks,
            NULL
        )
    );

    ESP_ERROR_CHECK(rmt_enable(rx_channel));

    ESP_LOGI(TAG, "Receptor IR inicializado!");

    ESP_ERROR_CHECK(
        rmt_receive(
            rx_channel,
            raw_symbols,
            sizeof(raw_symbols),
            &receive_config
        )
    );
}

void app_main(void) {

    ESP_LOGI(TAG, "================================");
    ESP_LOGI(TAG, " TESTE DE RECEPCAO INFRAVERMELHA");
    ESP_LOGI(TAG, " ESP32-S3 - ESP-IDF 6.1");
    ESP_LOGI(TAG, "================================");

    ir_receiver_init();

    while (1) {

        rmt_rx_done_event_data_t received_data;

        if (xQueueReceive(
                receive_queue,
                &received_data,
                portMAX_DELAY
            ) == pdTRUE) {

            ESP_LOGI(TAG, "SINAL INFRAVERMELHO RECEBIDO!");
            ESP_LOGI(TAG, "Quantidade de simbolos: %u",
                     (unsigned)received_data.num_symbols);

            for (size_t i = 0;
                 i < received_data.num_symbols;
                 i++) {

                const rmt_symbol_word_t *symbol =
                    &received_data.received_symbols[i];

                printf(
                    "[%03u] Nivel %u: %u us | Nivel %u: %u us\n",
                    (unsigned)i,
                    symbol->level0,
                    symbol->duration0,
                    symbol->level1,
                    symbol->duration1
                );
            }

            printf("\n");

            // Reinicia a captura para o próximo comando
            ESP_ERROR_CHECK(
                rmt_receive(
                    rx_channel,
                    raw_symbols,
                    sizeof(raw_symbols),
                    &receive_config
                )
            );

            ESP_LOGI(TAG, "Aguardando proximo sinal...");
        }
    }
}
