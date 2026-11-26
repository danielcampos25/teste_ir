
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "driver/rmt_tx.h"
#include "driver/rmt_encoder.h"
#include "esp_err.h"
#include "esp_log.h"

#define IR_TX_GPIO          5
#define RMT_RESOLUTION_HZ   1000000
#define IR_CARRIER_HZ       38000

#define HEADER_MARK_US      9000
#define HEADER_SPACE_US     4500
#define BIT_MARK_US         500
#define BIT_ZERO_SPACE_US   590
#define BIT_ONE_SPACE_US    1730
#define FOOTER_MARK_US      500

#define FRAME_BYTES         13
#define FRAME_SYMBOLS       106
#define MIN_TEMPERATURE     16
#define MAX_TEMPERATURE     32

static const char *TAG = "IR_TX";

static rmt_channel_handle_t tx_channel = NULL;
static rmt_encoder_handle_t copy_encoder = NULL;

typedef enum {
    IR_COMMAND_POWER,
    IR_COMMAND_TEMPERATURE_UP,
    IR_COMMAND_TEMPERATURE_DOWN,
} ir_command_t;

static uint8_t current_temperature = 21;
static bool air_conditioner_on = false;

static void add_symbol(
    rmt_symbol_word_t *symbols,
    int *index,
    uint16_t duration_mark,
    uint16_t duration_space
) {
    symbols[*index].level0 = 1;
    symbols[*index].duration0 = duration_mark;

    symbols[*index].level1 = 0;
    symbols[*index].duration1 = duration_space;

    (*index)++;
}

static uint8_t reverse_bits(uint8_t value)
{
    uint8_t reversed = 0;

    for (int bit = 0; bit < 8; bit++) {
        reversed = (uint8_t)((reversed << 1) | (value & 1));
        value >>= 1;
    }

    return reversed;
}

static uint8_t temperature_code(uint8_t temperature)
{
    uint8_t encoded = (uint8_t)(0x6F + ((temperature - 21) * 8));
    return reverse_bits(encoded);
}

static bool build_frame(ir_command_t command, uint8_t *frame)
{
    if (current_temperature < MIN_TEMPERATURE ||
        current_temperature > MAX_TEMPERATURE) {
        ESP_LOGE(TAG, "Temperatura fora do intervalo suportado: %u C",
                 current_temperature);
        return false;
    }

    memcpy(frame, (uint8_t[]) {
        0xC3, temperature_code(current_temperature), 0x07, 0x00, 0x04, 0x00, 0x02,
        0x00, 0x00, 0x04, 0x00, 0x00, 0x00
    }, FRAME_BYTES);

    uint8_t command_code;
    if (command == IR_COMMAND_POWER) {
        frame[9] = air_conditioner_on ? 0x04 : 0x00;
        command_code = 0xA0;
    } else if (command == IR_COMMAND_TEMPERATURE_UP) {
        command_code = 0x00;
    } else {
        command_code = 0x80;
    }

    frame[11] = command_code;
    uint8_t reversed_code = reverse_bits(frame[1]);
    if (command == IR_COMMAND_POWER && air_conditioner_on) {
        frame[12] = reverse_bits((uint8_t)(reversed_code + 0x28));
    } else if (command == IR_COMMAND_POWER) {
        frame[12] = reverse_bits((uint8_t)(reversed_code + 0x08));
    } else if (command == IR_COMMAND_TEMPERATURE_UP) {
        frame[12] = current_temperature == 21
                        ? 0x59
                        : reverse_bits((uint8_t)(reversed_code + 0x23));
    } else {
        frame[12] = reverse_bits((uint8_t)(reversed_code + 0x24));
    }

    return true;
}

static bool transmit_command(ir_command_t command)
{
    uint8_t frame[FRAME_BYTES];

    if (!build_frame(command, frame)) {
        return false;
    }

    ESP_LOGI(TAG, "Comando %d: temperatura=%u C, estado=%s",
             command, current_temperature,
             air_conditioner_on ? "ligado" : "desligado");

    rmt_symbol_word_t symbols[FRAME_SYMBOLS];
    int index = 0;

    /* Cabeçalho */
    add_symbol(
        symbols,
        &index,
        HEADER_MARK_US,
        HEADER_SPACE_US
    );

    /* 104 bits: transmissão MSB first */
    for (int i = 0; i < FRAME_BYTES; i++) {

        for (int bit = 7; bit >= 0; bit--) {

            uint8_t value = (frame[i] >> bit) & 0x01;

            if (value) {
                add_symbol(
                    symbols,
                    &index,
                    BIT_MARK_US,
                    BIT_ONE_SPACE_US
                );
            } else {
                add_symbol(
                    symbols,
                    &index,
                    BIT_MARK_US,
                    BIT_ZERO_SPACE_US
                );
            }
        }
    }

    /* Marca final */
    symbols[index].level0 = 1;
    symbols[index].duration0 = FOOTER_MARK_US;
    symbols[index].level1 = 0;
    symbols[index].duration1 = 0;

    index++;

    ESP_LOGI(TAG, "Transmitindo quadro com %d simbolos", index);

    rmt_transmit_config_t tx_config = {
        .loop_count = 0
    };

    ESP_ERROR_CHECK(
        rmt_transmit(
            tx_channel,
            copy_encoder,
            symbols,
            index * sizeof(rmt_symbol_word_t),
            &tx_config
        )
    );

    ESP_ERROR_CHECK(
        rmt_tx_wait_all_done(tx_channel, -1)
    );

    ESP_LOGI(TAG, "Transmissao concluida");
    return true;
}

static bool change_temperature(int8_t delta)
{
    uint8_t previous_temperature = current_temperature;
    int next_temperature = (int)current_temperature + delta;

    if (next_temperature < MIN_TEMPERATURE ||
        next_temperature > MAX_TEMPERATURE) {
        ESP_LOGW(TAG, "Temperatura limite atingida: %u C",
                 current_temperature);
        return false;
    }

    current_temperature = (uint8_t)next_temperature;
    bool transmitted = transmit_command(
        delta > 0 ? IR_COMMAND_TEMPERATURE_UP : IR_COMMAND_TEMPERATURE_DOWN);
    if (!transmitted) {
        current_temperature = previous_temperature;
    }
    return transmitted;
}

static bool ligar(void)
{
    air_conditioner_on = true;
    return transmit_command(IR_COMMAND_POWER);
}

static void init_ir_transmitter(void)
{
    /* Configuração do canal RMT */
    rmt_tx_channel_config_t tx_config = {
        .gpio_num = IR_TX_GPIO,
        .clk_src = RMT_CLK_SRC_DEFAULT,
        .resolution_hz = RMT_RESOLUTION_HZ,
        .mem_block_symbols = 128,
        .trans_queue_depth = 4,
        .flags.invert_out = false,
        .flags.with_dma = false
    };

    ESP_ERROR_CHECK(
        rmt_new_tx_channel(&tx_config, &tx_channel)
    );

    /* Portadora de 38 kHz */
    rmt_carrier_config_t carrier_config = {
        .frequency_hz = IR_CARRIER_HZ,
        .duty_cycle = 0.33,
        .flags.polarity_active_low = false,
        .flags.always_on = false
    };

    ESP_ERROR_CHECK(
        rmt_apply_carrier(tx_channel, &carrier_config)
    );

    /* Encoder para copiar os símbolos RMT */
    rmt_copy_encoder_config_t encoder_config = {};

    ESP_ERROR_CHECK(
        rmt_new_copy_encoder(&encoder_config, &copy_encoder)
    );

    ESP_ERROR_CHECK(rmt_enable(tx_channel));

    ESP_LOGI(TAG, "Transmissor IR inicializado no GPIO %d", IR_TX_GPIO);
}

static bool desligar(void)
{
    air_conditioner_on = false;
    return transmit_command(IR_COMMAND_POWER);
}

static bool aumentar_temperatura(void)
{
    return change_temperature(1);
}

static bool diminuir_temperatura(void)
{
    return change_temperature(-1);
}

void app_main(void)
{
    init_ir_transmitter();

    ESP_LOGI(TAG, "Iniciando teste automatico em 3 segundos");
    vTaskDelay(pdMS_TO_TICKS(3000));

    ligar();
    vTaskDelay(pdMS_TO_TICKS(10000));

    aumentar_temperatura();
    vTaskDelay(pdMS_TO_TICKS(10000));

    diminuir_temperatura();
    vTaskDelay(pdMS_TO_TICKS(10000));

    desligar();
}
