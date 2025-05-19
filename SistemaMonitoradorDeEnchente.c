#include "pico/stdlib.h"
#include "hardware/gpio.h"
#include "hardware/i2c.h"
#include "hardware/pio.h"
#include "hardware/adc.h"
#include "hardware/clocks.h"
#include "hardware/pwm.h"

#include "ws2818b.pio.h"
#include "figures.h"

#include "lib/ssd1306.h"
#include "lib/font.h"
#include "img_disp.h"

#include "FreeRTOS.h"
#include "FreeRTOSConfig.h"
#include "task.h"
#include "queue.h"

#include <stdio.h>


// Valores do Display SSD1306
#define I2C_PORT i2c1
#define I2C_SDA 14
#define I2C_SCL 15
#define endereco 0x3C

// Joystick
#define ADC_JOYSTICK_X 26
#define ADC_JOYSTICK_Y 27

// LED RGB
#define ledR 13
#define ledG 11

// Definição do número de LEDs e pino - ws2818b
#define LED_COUNT 25
#define LED_PIN 7

// Definição de pixel GRB
struct pixel_t {
    uint8_t G, R, B; // Três valores de 8-bits compõem um pixel.
};
typedef struct pixel_t pixel_t;
typedef pixel_t npLED_t; // Mudança de nome de "struct pixel_t" para "npLED_t" por clareza.

// Declaração do buffer de pixels que formam a matriz.
npLED_t leds[LED_COUNT];

// Variáveis para uso da máquina PIO.
PIO np_pio;
uint sm;

/**
 * Inicializa a máquina PIO para controle da matriz de LEDs.
 */
void npInit(uint pin) {
    
    // Cria programa PIO.
    uint offset = pio_add_program(pio0, &ws2818b_program);
    np_pio = pio0;
    
    // Toma posse de uma máquina PIO.
    sm = pio_claim_unused_sm(np_pio, false);
    if (sm < 0) {
        np_pio = pio1;
        sm = pio_claim_unused_sm(np_pio, true); // Se nenhuma máquina estiver livre, panic!
    }
    
    // Inicia programa na máquina PIO obtida.
    ws2818b_program_init(np_pio, sm, offset, pin, 800000.f);
    
    // Limpa buffer de pixels.
    for (uint i = 0; i < LED_COUNT; ++i) {
        leds[i].R = 0;
        leds[i].G = 0;
        leds[i].B = 0;
    }
}

/**
 * Atribui uma cor RGB a um LED.
 */
void npSetLED(const uint index, const uint8_t r, const uint8_t g, const uint8_t b) {
    leds[index].R = r;
    leds[index].G = g;
    leds[index].B = b;
}

/**
 * Limpa o buffer de pixels.
 */
void npClear() {
    for (uint i = 0; i < LED_COUNT; ++i)
    npSetLED(i, 0, 0, 0);
}

/**
 * Escreve os dados do buffer nos LEDs.
 */
void npWrite() {
    // Escreve cada dado de 8-bits dos pixels em sequência no buffer da máquina PIO.
    for (uint i = 0; i < LED_COUNT; ++i) {
        pio_sm_put_blocking(np_pio, sm, leds[i].G);
        pio_sm_put_blocking(np_pio, sm, leds[i].R);
        pio_sm_put_blocking(np_pio, sm, leds[i].B);
    }
    sleep_us(100); // Espera 100us, sinal de RESET do datasheet.
}

ssd1306_t ssd; // Estrutura do display

// Buzzer
const uint8_t BUZZER_PIN = 21;
const uint16_t PERIOD = 59609; // WRAP
const float DIVCLK = 16.0; // Divisor inteiro
static uint slice_21;
const uint16_t dc_values[] = {PERIOD * 0.3, PERIOD * 0.5, PERIOD * 0.8, 0}; // Duty Cycle de 30% e 0%

void setup_pwm(){

    // PWM do BUZZER
    // Configura para soar 440 Hz
    gpio_set_function(BUZZER_PIN, GPIO_FUNC_PWM);
    slice_21 = pwm_gpio_to_slice_num(BUZZER_PIN);
    pwm_set_clkdiv(slice_21, DIVCLK);
    pwm_set_wrap(slice_21, PERIOD);
    pwm_set_gpio_level(BUZZER_PIN, 0);
    pwm_set_enabled(slice_21, true);
}

// Display SSD1306
void initialize_i2c(){
    i2c_init(I2C_PORT, 400 * 1000); // Frequência de transmissão de 400 khz
    gpio_set_function(I2C_SDA, GPIO_FUNC_I2C); // GPIO para função de I2C
    gpio_set_function(I2C_SCL, GPIO_FUNC_I2C); // GPIO para função de I2C
    gpio_pull_up(I2C_SDA); 
    gpio_pull_up(I2C_SCL); 
}

// Desenha os valores das porcentagens
void draw_percs(uint16_t w, uint16_t r){

    // Apaga os valores anteriores
    ssd1306_draw_string(&ssd, "   ", 24, 12);
    ssd1306_draw_string(&ssd, "   ", 63, 12);

    // Escreve os novos valores no display
    char per_water_level[4], per_rainfall_volume[4];
    sprintf(per_water_level, "%d", w);
    sprintf(per_rainfall_volume, "%d", r);
    ssd1306_draw_string(&ssd, per_water_level, 24, 12);
    ssd1306_draw_string(&ssd, per_rainfall_volume, 63, 12);
    ssd1306_send_data(&ssd);
}

// LED RGB
void init_ledRGB(){
    gpio_init(ledR);
    gpio_set_dir(ledR, GPIO_OUT);
    gpio_put(ledR, false);

    gpio_init(ledG);
    gpio_set_dir(ledG, GPIO_OUT);
    gpio_put(ledG, false);
}


// >>>>>>>>>>>>> TAREFAS <<<<<<<<<<<<<<
// Níveis normais de água e chuva
const uint WATER_MAX = 80;
const uint RAIN_MAX = 70;

// Estrutura para armazenar os valores dos sensores
typedef struct
{
    uint16_t x_adc; // Valor do nível da água
    uint16_t y_adc; // Valor do Volume da Chuva
} joystick_data_t;

QueueHandle_t xQueueJoystickData; // Para gerenciar a fila dos valores dos sensores

// Filas para armazenar o modo atual e executar no respectivo periférico
QueueHandle_t xQueueModeLedRgb; 
QueueHandle_t xQueueModeMatrix;
QueueHandle_t xQueueModeBuzzer;

// Tarefa para ler os valores dos sensores
void vJoystickTask(void *params)
{
    adc_gpio_init(ADC_JOYSTICK_Y);
    adc_gpio_init(ADC_JOYSTICK_X);
    adc_init();

    joystick_data_t joydata;

    while (true)
    {
        adc_select_input(0); // GPIO 26 = ADC0
        joydata.y_adc = adc_read(); // Lê o volume da chuva

        adc_select_input(1); // GPIO 27 = ADC1
        joydata.x_adc = adc_read(); // Lê o nível da água

        xQueueSend(xQueueJoystickData, &joydata, 0); // Envia o valor do joystick para a fila
        vTaskDelay(pdMS_TO_TICKS(100));              // 10 Hz de leitura
    }
}

// Tarefa para executar as ações do display
void vDisplayTask(void *params)
{
    initialize_i2c();
    ssd1306_init(&ssd, WIDTH, HEIGHT, false, endereco, I2C_PORT);
    ssd1306_config(&ssd);
    ssd1306_send_data(&ssd);
    ssd1306_draw_bitmap(&ssd, display_data); // Desenha a tela padrão do sistema

    joystick_data_t joydata;
    uint16_t rainfall_volume, water_level;
    bool mode = false; // Guarda o modo atual. True para modo de alerta e False para modo normal

    bool cor = true;
    while (true)
    {
        // Se os valores dos sensores foram recebidos, executam-se as ações do display
        if (xQueueReceive(xQueueJoystickData, &joydata, portMAX_DELAY) == pdTRUE)
        {
            rainfall_volume = (joydata.y_adc * 100) / 4095; // Porcentagem de volume da chuva
            water_level = (joydata.x_adc * 100) / 4095; // Porcentagem de nível da água
            
            ssd1306_send_data(&ssd);

            //Exibe as barras de porcentagem no display
            ssd1306_levels(&ssd, water_level, 11, 59); // Barra do Nível de Água
            ssd1306_levels(&ssd, rainfall_volume, 49, 59); // Barra do Volume de Chuva

            ssd1306_draw_string(&ssd, "       ", 75, 38); // Apaga mensagens anteriores no display
            draw_percs(water_level, rainfall_volume); // Desenha as porcentagens no display

            // Se os níveis estiverem anormais, ativa o modo de alerta e exibe a mensagem no display
            if (rainfall_volume >= RAIN_MAX || water_level >= WATER_MAX){
                mode = true; 
                ssd1306_draw_string(&ssd, "ALERTA", 75, 38);
                ssd1306_send_data(&ssd);

            }

            else{
                mode = false; // Alterna para o modo normal
            }
        }

        xQueueSend(xQueueModeLedRgb, &mode, 0);
        xQueueSend(xQueueModeMatrix, &mode, 0);
        xQueueSend(xQueueModeBuzzer, &mode, 0);
    }
}

// Tarefa para executar o LED RGB
void vLedRgbTask(void *params)
{
    init_ledRGB();
    bool mode; // Guarda o modo atual. True para modo de alerta e False para modo normal

    while (true)
    {
        if (xQueueReceive(xQueueModeLedRgb, &mode, portMAX_DELAY) == pdTRUE)
        {
            if (mode){ // Se for o modo de alerta, pisca o LED vermelho
                gpio_put(ledG, false);
                gpio_put(ledR, true);
                vTaskDelay(pdMS_TO_TICKS(100));
                gpio_put(ledR, false);
                vTaskDelay(pdMS_TO_TICKS(100));
            }

            else{
                gpio_put(ledG, true);
            }
        }
        vTaskDelay(pdMS_TO_TICKS(10)); // Atualiza a cada 50ms
    }
}

// Tarefa para executar a Matriz de LEDs
void vMatrixTask(void *params){
    npInit(LED_PIN);
    npClear();
    npWrite();

    bool mode; // Guarda o modo atual. True para modo de alerta e False para modo normal

    while (true){
        if (xQueueReceive(xQueueModeMatrix, &mode, portMAX_DELAY) == pdTRUE){
            // Se o modo de alerta for ativado, exibe um sinal de alerta na matriz
            if (mode){
                for (int i = 0; i < 25; i++)
                    npSetLED(i, warning[i][0], warning[i][1], warning[i][2]);
                npWrite();
            }

            else{
                npClear();
                npWrite();
            }
        }

        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

// Tarefa para executar o Buzzer
void vBuzzerTask(void *params){
    setup_pwm();
    bool mode; // Guarda o modo atual. True para modo de alerta e False para modo normal

    while(true){

        // Se for o modo de alerta, aciona um alarme sonoro
        if (xQueueReceive(xQueueModeBuzzer, &mode, portMAX_DELAY) == pdTRUE){
            if (mode){
                pwm_set_gpio_level(BUZZER_PIN, dc_values[0]);
                vTaskDelay(pdMS_TO_TICKS(50));
                pwm_set_gpio_level(BUZZER_PIN, dc_values[1]);
                vTaskDelay(pdMS_TO_TICKS(50));
                pwm_set_gpio_level(BUZZER_PIN, dc_values[2]);
                vTaskDelay(pdMS_TO_TICKS(50));
            }

            else{
                pwm_set_gpio_level(BUZZER_PIN, dc_values[3]);
            }
        }

        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

int main()
{
    stdio_init_all();

    // Cria a fila para compartilhamento de valor do joystick e outras para enviar o modo do sistema às demais tarefas
    xQueueJoystickData = xQueueCreate(5, sizeof(joystick_data_t));
    xQueueModeLedRgb = xQueueCreate(3, sizeof(bool)); 
    xQueueModeMatrix = xQueueCreate(3, sizeof(bool));
    xQueueModeBuzzer = xQueueCreate(3, sizeof(bool));

    // Criação das tasks
    xTaskCreate(vJoystickTask, "Joystick Task", 256, NULL, 1, NULL);     // Captura os dados do nível de água e volume da chuva
    xTaskCreate(vDisplayTask, "Display Task", 512, NULL, 1, NULL);       // Calcula e exibe os dados no display
    xTaskCreate(vLedRgbTask, "LED RGB Task", 256, NULL, 1, NULL);        // Controla o LED RGB
    xTaskCreate(vMatrixTask, "Matriz LED Task", 256, NULL, 1, NULL);     // Mostra alerta na matriz de LEDs
    xTaskCreate(vBuzzerTask, "Buzzer Task", 256, NULL, 1, NULL);         // Controla o buzzer

    // Inicia o agendador
    vTaskStartScheduler();
    panic_unsupported();
}
