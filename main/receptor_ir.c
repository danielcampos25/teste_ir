
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"

#include "driver/rmt_rx.h"
#include "esp_log.h"
#include "esp_err.h"

#define IR_RX_GPIO              4
#define IR_RESOLUTION_HZ        1000000
#define IR_MEM_BLOCK_SYMBOLS    128
#define IR_BUFFER_SYMBOLS       256 

#define FRAME_BYTES             13
#define FRAME_SYMBOLS           (1 + (FRAME_BYTES * 8) + 1)

#define FRAME_HEADER_MARK_MIN   8000
#define FRAME_HEADER_MARK_MAX   10000
#define FRAME_HEADER_SPACE_MIN  4000
#define FRAME_HEADER_SPACE_MAX  5000

#define FRAME_BIT_MARK_MIN      350
#define FRAME_BIT_MARK_MAX      700
#define FRAME_BIT_ZERO_MIN      400
#define FRAME_BIT_ZERO_MAX      900
#define FRAME_BIT_ONE_MIN       1300
#define FRAME_BIT_ONE_MAX       2100

static const char *TAG = "TESTE_IR";

static rmt_channel_handle_t rx_channel;
static QueueHandle_t receive_queue;

static rmt_symbol_word_t raw_symbols[IR_BUFFER_SYMBOLS];

typedef struct {
    const rmt_symbol_word_t *symbols;
    size_t num_symbols;
} ir_rx_event_t;

static uint8_t last_frame[FRAME_BYTES];
static bool last_frame_valid = false;

/*
 * Callback chamado quando uma captura termina.
 */
static bool IRAM_ATTR rmt_rx_done_callback(
    rmt_channel_handle_t channel,
    const rmt_rx_done_event_data_t *edata,
    void *user_data)
{
    BaseType_t higher_task_woken = pdFALSE;

    ir_rx_event_t event = {
        .symbols = edata->received_symbols,
        .num_symbols = edata->num_symbols
    };

    xQueueSendFromISR(receive_queue, &event, &higher_task_woken);

    return higher_task_woken == pdTRUE;
}

/*
 * Verifica o cabeçalho do quadro do controle Elgin.
 */
static bool is_frame_header(const rmt_symbol_word_t *symbol)
{
    return (
        ((symbol->level0 == 1 && symbol->level1 == 0) ||
         (symbol->level0 == 0 && symbol->level1 == 1)) &&
        symbol->duration0 >= FRAME_HEADER_MARK_MIN &&
        symbol->duration0 <= FRAME_HEADER_MARK_MAX &&
        symbol->duration1 >= FRAME_HEADER_SPACE_MIN &&
        symbol->duration1 <= FRAME_HEADER_SPACE_MAX
    );
}

/*
 * Decodifica o quadro de 13 bytes transmitido pelo controle.
 * Este protocolo transmite cada byte do bit mais significativo para o menos
 * significativo.
 */
static bool decode_frame(
    const rmt_symbol_word_t *symbols,
    size_t num_symbols,
    uint8_t *frame)
{
    if (num_symbols < FRAME_SYMBOLS ||
        !is_frame_header(&symbols[0])) {
        return false;
    }

    memset(frame, 0, FRAME_BYTES);

    const uint8_t mark_level = symbols[0].level0;
    const uint8_t space_level = symbols[0].level1;

    for (int bit_index = 0; bit_index < FRAME_BYTES * 8; bit_index++) {
        const rmt_symbol_word_t *symbol = &symbols[bit_index + 1];

        if (symbol->level0 != mark_level ||
            symbol->level1 != space_level ||
            symbol->duration0 < FRAME_BIT_MARK_MIN ||
            symbol->duration0 > FRAME_BIT_MARK_MAX) {
            return false;
        }

        uint8_t bit;
        if (symbol->duration1 >= FRAME_BIT_ZERO_MIN &&
            symbol->duration1 <= FRAME_BIT_ZERO_MAX) {
            bit = 0;
        }
        else if (symbol->duration1 >= FRAME_BIT_ONE_MIN &&
                 symbol->duration1 <= FRAME_BIT_ONE_MAX) {
            bit = 1;
        }
        else {
            return false;
        }

        frame[bit_index / 8] =
            (uint8_t)((frame[bit_index / 8] << 1) | bit);
    }

    return true;
}

/*
 * Exibe o quadro completo em bytes para permitir correlacionar os comandos
 * de temperatura, incremento e decremento.
 */
static void log_frame(const uint8_t *frame)
{
    printf("Quadro:");
    for (int i = 0; i < FRAME_BYTES; i++) {
        printf(" %02X", frame[i]);
    }
    printf("\n");

    ESP_LOGI(TAG, "Campo de controle candidato: byte[9] = 0x%02X",
             frame[9]);
}

/*
 * Exibe os dados capturados.
 */

static void process_ir_signal(const rmt_symbol_word_t *symbols,
                              size_t num_symbols)
{
    if (num_symbols != FRAME_SYMBOLS) {
        return;
    }

    uint8_t frame[FRAME_BYTES];
    if (!decode_frame(symbols, num_symbols, frame)) {
        return;
    }

    ESP_LOGI(TAG, "====================================");
    ESP_LOGI(TAG, "Sinal recebido: %u simbolos",
             (unsigned int)num_symbols);
    log_frame(frame);

    if (last_frame_valid) {
        for (int i = 0; i < FRAME_BYTES; i++) {
            if (frame[i] != last_frame[i]) {
                ESP_LOGI(TAG, "Alteracao: byte[%d] 0x%02X -> 0x%02X",
                         i, last_frame[i], frame[i]);
            }
        }
    } else {
        ESP_LOGI(TAG, "Primeiro quadro valido recebido.");
    }

    memcpy(last_frame, frame, FRAME_BYTES);
    last_frame_valid = true;

    ESP_LOGI(TAG, "====================================");
}


void app_main(void)


{
    ESP_LOGI(TAG, "Iniciando receptor infravermelho");

    receive_queue = xQueueCreate(5, sizeof(ir_rx_event_t));

    if (receive_queue == NULL) {
        ESP_LOGE(TAG, "Falha ao criar fila");
        return;
    }

    rmt_rx_channel_config_t rx_config = {
        .gpio_num = IR_RX_GPIO,
        .clk_src = RMT_CLK_SRC_DEFAULT,
        .resolution_hz = IR_RESOLUTION_HZ,
        .mem_block_symbols = IR_MEM_BLOCK_SYMBOLS,
        .flags.invert_in = false,
        .flags.with_dma = false
    };

    ESP_ERROR_CHECK(
        rmt_new_rx_channel(&rx_config, &rx_channel)
    );

    rmt_rx_event_callbacks_t callbacks = {
        .on_recv_done = rmt_rx_done_callback
    };

    ESP_ERROR_CHECK(
        rmt_rx_register_event_callbacks(
            rx_channel,
            &callbacks,
            NULL
        )
    );

    ESP_ERROR_CHECK(rmt_enable(rx_channel));

    rmt_receive_config_t receive_config = {
        .signal_range_min_ns = 1000,
        .signal_range_max_ns = 30000000,
    };

    ESP_LOGI(TAG, "Aguardando sinais IR no GPIO%d", IR_RX_GPIO);

    while (1) {

        ESP_ERROR_CHECK(
            rmt_receive(
                rx_channel,
                raw_symbols,
                sizeof(raw_symbols),
                &receive_config
            )
        );

        ir_rx_event_t event;

        if (xQueueReceive(
                receive_queue,
                &event,
                portMAX_DELAY) == pdTRUE) {

            process_ir_signal(
                event.symbols,
                event.num_symbols
            );
        }
    }
}
