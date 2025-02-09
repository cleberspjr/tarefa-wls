#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include "pico/stdlib.h"
#include "hardware/i2c.h"
#include "hardware/pio.h"
#include "hardware/clocks.h"
#include "inc/ssd1306.h"
#include "inc/font.h"
#include "inter_rp_2040.pio.h"

#define I2C_PORT i2c1
#define I2C_SDA 14
#define I2C_SCL 15
#define ADD 0x3C
#define LED_MATRIX_PIN 7
#define GREEN_LED_PIN 11
#define BLUE_LED_PIN 12
#define BTN_A_PIN 5
#define BTN_B_PIN 6

// Matriz 5x5 que mostra os números
const uint32_t numbers[10][25] = {
    {1,1,1,1,1,  1,0,0,0,1,  1,0,0,0,1,  1,0,0,0,1,  1,1,1,1,1}, // 0
    {1,1,1,1,1,  0,0,1,0,0,  0,0,1,0,1,  0,1,1,0,0,  0,0,1,0,0}, // 1
    {1,1,1,1,1,  1,0,0,0,0,  1,1,1,1,1,  0,0,0,0,1,  1,1,1,1,1}, // 2
    {1,1,1,1,1,  0,0,0,0,1,  0,1,1,1,1,  0,0,0,0,1,  1,1,1,1,1}, // 3
    {1,0,0,0,0,  0,0,0,0,1,  1,1,1,1,1,  1,0,0,0,1,  1,0,0,0,1}, // 4
    {1,1,1,1,1,  0,0,0,0,1,  1,1,1,1,1,  1,0,0,0,0,  1,1,1,1,1}, // 5
    {1,1,1,1,1,  1,0,0,0,1,  1,1,1,1,1,  1,0,0,0,0,  1,1,1,1,1}, // 6
    {0,0,0,1,0,  0,0,1,0,0,  0,1,0,0,0,  0,0,0,0,1,  1,1,1,1,1}, // 7
    {1,1,1,1,1,  1,0,0,0,1,  1,1,1,1,1,  1,0,0,0,1,  1,1,1,1,1}, // 8
    {1,1,1,1,1,  0,0,0,0,1,  1,1,1,1,1,  1,0,0,0,1,  1,1,1,1,1}, // 9
};

// Cores para os números
int number_colors[10][3] = {
    {2, 0, 0}, {2, 2, 0}, {46, 28, 12}, {1, 0, 1}, {0, 2, 2},
    {2, 1, 0}, {0, 2, 1}, {0, 0, 2}, {2, 2, 0}, {0, 2, 0},
};

// Estrutura para controlar a matriz de LEDs WS2812
typedef struct {
    uint8_t G, R, B; // Valores de cor para cada LED
} ws2812b_LED_t;

ws2812b_LED_t led_matrix[25]; // Buffer para a matriz de LEDs (5x5)
PIO led_matrix_pio;           // PIO usado para controlar a matriz
uint sm;                      // State machine do PIO

// Funções para controlar a matriz de LEDs WS2812
void ws2812b_init(uint pin);
void ws2812b_set_led(uint index, uint8_t r, uint8_t g, uint8_t b);
void ws2812b_clear();
void ws2812b_write();
void ws2812b_draw_number(uint8_t number_index);

void init_led(uint8_t led_pin);
void init_btn(uint8_t btn_pin);
void init_i2c();
void init_display(ssd1306_t *ssd);
void update_led_messages();
static void gpio_irq_handler(uint gpio, uint32_t events);

static volatile uint32_t last_time_btn_a = 0; 
static volatile uint32_t last_time_btn_b = 0; 
static volatile bool green_led_state = false; 
static volatile bool blue_led_state = false; 
static char green_led_state_message[20] = "Verde OFF.";
static char blue_led_state_message[20] = "Azul OFF."; 

// Inicializa o display OLED
ssd1306_t ssd; // Inicializa a estrutura do display

int main() {
    stdio_init_all();

    // Inicializa os LEDs
    init_led(GREEN_LED_PIN);
    init_led(BLUE_LED_PIN);

    // Inicializa os botões
    init_btn(BTN_A_PIN);
    init_btn(BTN_B_PIN);

    // Inicializa a comunicação I2C
    init_i2c();

    // Inicializa o display
    init_display(&ssd);

    // Inicializa a matriz de LEDs WS2812
    ws2812b_init(LED_MATRIX_PIN);

    // Exibe as mensagens iniciais no display
    ssd1306_fill(&ssd, false); // Limpa o display
    ssd1306_draw_string(&ssd, green_led_state_message, 10, 10);
    ssd1306_draw_string(&ssd, blue_led_state_message, 10, 50);
    ssd1306_send_data(&ssd); // Atualiza o display

    // Habilita interrupções nos botões
    gpio_set_irq_enabled_with_callback(BTN_A_PIN, GPIO_IRQ_EDGE_FALL, true, &gpio_irq_handler);
    gpio_set_irq_enabled_with_callback(BTN_B_PIN, GPIO_IRQ_EDGE_FALL, true, &gpio_irq_handler);

    printf("Sistema inicializado. Aguardando interações...\n");

    while (true) {
        // Verifica se há caracteres disponíveis no Serial Monitor
        if (stdio_usb_connected()) {
            char character = getchar();
            if (character != PICO_ERROR_TIMEOUT) {
                printf("Caractere recebido: %c\n", character);

                // Exibe o caractere no display SSD1306
                ssd1306_fill(&ssd, false); // Limpa o display
                ssd1306_draw_char(&ssd, character, 58, 30);
                ssd1306_draw_string(&ssd, green_led_state_message, 10, 10);
                ssd1306_draw_string(&ssd, blue_led_state_message, 10, 50);
                ssd1306_send_data(&ssd); // Atualiza o display

                // Se o caractere for um número, exibe na matriz de LEDs
                if (isdigit(character)) {
                    uint8_t number = character - '0';
                    ws2812b_draw_number(number); // Desenha o número na matriz de LEDs
                } else {
                    // Limpa a matriz de LEDs se não for um número
                    ws2812b_clear();
                    ws2812b_write();
                }
            }
        }

        sleep_ms(100);
    }
}

// Inicializa um LED em um pino específico
void init_led(uint8_t led_pin) {
    gpio_init(led_pin);
    gpio_set_dir(led_pin, GPIO_OUT);
    gpio_put(led_pin, false); // Inicia desligado
}

// Inicializa um botão em um pino específico
void init_btn(uint8_t btn_pin) {
    gpio_init(btn_pin);
    gpio_set_dir(btn_pin, GPIO_IN);
    gpio_pull_up(btn_pin);
}

// Inicializa a comunicação I2C
void init_i2c() {
    i2c_init(I2C_PORT, 400 * 1000);
    gpio_set_function(I2C_SDA, GPIO_FUNC_I2C);
    gpio_set_function(I2C_SCL, GPIO_FUNC_I2C);
    gpio_pull_up(I2C_SDA);
    gpio_pull_up(I2C_SCL);
}

// Inicializa o display OLED
void init_display(ssd1306_t *ssd) {
    ssd1306_init(ssd, 128, 64, false, ADD, I2C_PORT);
    ssd1306_config(ssd);
    ssd1306_send_data(ssd);

    // Limpa o display. O display inicia com todos os pixels apagados.
    ssd1306_fill(ssd, false);
    ssd1306_send_data(ssd);
}

// Atualiza as mensagens exibidas no display
void update_led_messages() {
    if (green_led_state)
        strcpy(green_led_state_message, "Verde ON.");
    else
        strcpy(green_led_state_message, "Verde OFF.");

    if (blue_led_state)
        strcpy(blue_led_state_message, "Azul ON.");
    else
        strcpy(blue_led_state_message, "Azul OFF.");
}

// Função de callback para as interrupções dos botões
void gpio_irq_handler(uint gpio, uint32_t events) {
    uint32_t current_time = to_us_since_boot(get_absolute_time());

    // Verifica qual botão foi pressionado.
    if (gpio == BTN_A_PIN && (current_time - last_time_btn_a > 200000)) {
        // Atualiza o tempo do último evento do botão A.
        last_time_btn_a = current_time;
        green_led_state = !green_led_state;

        // Atualiza o estado do LED verde
        gpio_put(GREEN_LED_PIN, green_led_state);

        // Atualiza as mensagens no display
        update_led_messages();
        ssd1306_fill(&ssd, false); // Limpa o display
        ssd1306_draw_string(&ssd, green_led_state_message, 10, 10);
        ssd1306_draw_string(&ssd, blue_led_state_message, 10, 50);
        ssd1306_send_data(&ssd); // Atualiza o display

        printf("%s\n", green_led_state_message);
    } else if (gpio == BTN_B_PIN && (current_time - last_time_btn_b > 200000)) {
        // Atualiza o tempo do último evento do botão B.
        last_time_btn_b = current_time;
        blue_led_state = !blue_led_state;

        // Atualiza o estado do LED azul
        gpio_put(BLUE_LED_PIN, blue_led_state);

        // Atualiza as mensagens no display
        update_led_messages();
        ssd1306_fill(&ssd, false); // Limpa o display
        ssd1306_draw_string(&ssd, green_led_state_message, 10, 10);
        ssd1306_draw_string(&ssd, blue_led_state_message, 10, 50);
        ssd1306_send_data(&ssd); // Atualiza o display

        printf("%s\n", blue_led_state_message);
    }
}

// Inicializa a matriz de LEDs WS2812
void ws2812b_init(uint pin) {
    uint offset = pio_add_program(pio0, &inter_rp_2040_program);
    led_matrix_pio = pio0;
    sm = pio_claim_unused_sm(led_matrix_pio, true);
    inter_rp_2040_program_init(led_matrix_pio, sm, offset, pin, 800000.f);

    // Limpa a matriz de LEDs
    ws2812b_clear();
    ws2812b_write();
}

// Define a cor de um LED específico
void ws2812b_set_led(uint index, uint8_t r, uint8_t g, uint8_t b) {
    led_matrix[index].R = r;
    led_matrix[index].G = g;
    led_matrix[index].B = b;
}

// Limpa a matriz de LEDs
void ws2812b_clear() {
    for (uint i = 0; i < 25; ++i) {
        ws2812b_set_led(i, 0, 0, 0);
    }
}

// Escreve os dados no buffer da matriz de LEDs
void ws2812b_write() {
    for (uint i = 0; i < 25; ++i) {
        pio_sm_put_blocking(led_matrix_pio, sm, led_matrix[i].G);
        pio_sm_put_blocking(led_matrix_pio, sm, led_matrix[i].R);
        pio_sm_put_blocking(led_matrix_pio, sm, led_matrix[i].B);
    }
    sleep_us(100); // Aguarda 100us para o sinal de RESET
}

// Desenha um número na matriz de LEDs
void ws2812b_draw_number(uint8_t number_index) {
    if (number_index >= 10) return; // Apenas números de 0 a 9

    // Define as cores dos LEDs com base no número
    for (int i = 0; i < 25; i++) {
        if (numbers[number_index][i]) {
            ws2812b_set_led(i, number_colors[number_index][0], number_colors[number_index][1], number_colors[number_index][2]);
        } else {
            ws2812b_set_led(i, 0, 0, 0); // Desliga o LED
        }
    }
    ws2812b_write(); // Atualiza a matriz de LEDs
}