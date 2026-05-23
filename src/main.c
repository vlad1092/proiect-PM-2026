#ifndef F_CPU
#define F_CPU 16000000UL
#endif

#include <avr/io.h>
#include <util/delay.h>
#include <stdint.h>
#include <stdlib.h>

#define LCD_ADDR 0x27
#define LCD_BACKLIGHT 0x08
#define LCD_ENABLE    0x04
#define LCD_RS        0x01

#define BTN_TOP     PD0
#define BTN_BOTTOM  PD1
#define BUZZER PC3

uint8_t row_pins[3] = { PD2, PD3, PD4 };
uint8_t col_pins[3] = { PD5, PD6, PD7 };

uint8_t led_ports[9] = {0,0,0,0,0,0,1,1,1}; 
uint8_t led_pins[9]  = {PB0,PB1,PB2,PB3,PB4,PB5,PC0,PC1,PC2};

uint8_t selected_game = 1;
uint16_t highscore_sequence = 0;
uint16_t highscore_visual = 0;

/* ================= I2C ================= */

void TWI_init(void)
{
    PORTC |= (1 << PC4) | (1 << PC5);
    TWSR = 0x00;
    TWBR = 72;
}

void TWI_start(void)
{
    TWCR = (1 << TWINT) | (1 << TWSTA) | (1 << TWEN);
    while (!(TWCR & (1 << TWINT)));
}

void TWI_stop(void)
{
    TWCR = (1 << TWINT) | (1 << TWSTO) | (1 << TWEN);
    _delay_us(20);
}

void TWI_write(uint8_t data)
{
    TWDR = data;
    TWCR = (1 << TWINT) | (1 << TWEN);
    while (!(TWCR & (1 << TWINT)));
}

/* ================= LCD ================= */

void LCD_expander_write(uint8_t data)
{
    TWI_start();
    TWI_write(LCD_ADDR << 1);
    TWI_write(data | LCD_BACKLIGHT);
    TWI_stop();
}

void LCD_pulse_enable(uint8_t data)
{
    LCD_expander_write(data | LCD_ENABLE);
    _delay_us(2);
    LCD_expander_write(data & ~LCD_ENABLE);
    _delay_us(100);
}

void LCD_write4(uint8_t data)
{
    LCD_expander_write(data);
    LCD_pulse_enable(data);
}

void LCD_send(uint8_t value, uint8_t mode)
{
    LCD_write4((value & 0xF0) | mode);
    LCD_write4(((value << 4) & 0xF0) | mode);
}

void LCD_command(uint8_t cmd)
{
    LCD_send(cmd, 0);
}

void LCD_data(uint8_t data)
{
    LCD_send(data, LCD_RS);
}

void LCD_clear(void)
{
    LCD_command(0x01);
    _delay_ms(3);
}

void LCD_init(void)
{
    _delay_ms(100);

    LCD_write4(0x30);
    _delay_ms(10);
    LCD_write4(0x30);
    _delay_ms(10);
    LCD_write4(0x30);
    _delay_ms(10);
    LCD_write4(0x20);
    _delay_ms(10);

    LCD_command(0x28);
    LCD_command(0x08);
    LCD_clear();
    LCD_command(0x06);
    LCD_command(0x0C);
}

void LCD_set_cursor(uint8_t row, uint8_t col)
{
    uint8_t addr = (row == 0) ? col : (0x40 + col);
    LCD_command(0x80 | addr);
}

void LCD_print(const char *str)
{
    while (*str) LCD_data(*str++);
}

void LCD_print_uint(uint16_t x)
{
    char buf[6];
    itoa(x, buf, 10);
    LCD_print(buf);
}

void LCD_print_line(uint8_t row, const char *text, char action)
{
    LCD_set_cursor(row, 0);

    uint8_t i = 0;

    while (text[i] && i < 15) {
        LCD_data(text[i]);
        i++;
    }

    while (i < 15) {
        LCD_data(' ');
        i++;
    }

    LCD_data(action);
}

/* ================= RANDOM ================= */

void seed_random(void)
{
    TCCR0B |= (1 << CS00) | (1 << CS02);

    uint16_t seed = 0;

    for (uint8_t i = 0; i < 32; i++) {
        seed ^= TCNT0;
        seed ^= (PIND << 1);
        seed = (seed << 1) | (seed >> 15);
        _delay_ms(5);
    }

    srand(seed);
}

void randomize_from_user(void)
{
    uint16_t seed = rand();
    seed ^= TCNT0;
    seed ^= (PIND << 3);
    srand(seed);
}

/* ================= BUZZER ================= */

void buzzer_init(void)
{
    DDRC |= (1 << BUZZER);
    PORTC &= ~(1 << BUZZER);
}

void buzz_delay(uint16_t freq)
{
    if (freq <= 300) _delay_us(1600);
    else if (freq <= 600) _delay_us(800);
    else if (freq <= 1000) _delay_us(500);
    else if (freq <= 1500) _delay_us(330);
    else _delay_us(250);
}

void beep(uint16_t freq, uint16_t duration_ms)
{
    uint16_t cycles = duration_ms * 2;

    for (uint16_t i = 0; i < cycles; i++) {
        PORTC |= (1 << BUZZER);
        buzz_delay(freq);
        PORTC &= ~(1 << BUZZER);
        buzz_delay(freq);
    }
}

void sound_menu(void)      { beep(900, 40); }
void sound_matrix(void)    { beep(1200, 40); }
void sound_success(void)   { beep(1600, 50); _delay_ms(60); beep(1800, 50); }
void sound_error(void)     { beep(250, 250); }
void sound_gameover(void)  { beep(400, 120); _delay_ms(80); beep(250, 180); }

/* ================= LEDS ================= */

void leds_init(void)
{
    DDRB |= (1 << PB0) | (1 << PB1) | (1 << PB2) |
            (1 << PB3) | (1 << PB4) | (1 << PB5);

    DDRC |= (1 << PC0) | (1 << PC1) | (1 << PC2);
}

void led_on(uint8_t index)
{
    if (index > 8) return;

    if (led_ports[index] == 0)
        PORTB |= (1 << led_pins[index]);
    else
        PORTC |= (1 << led_pins[index]);
}

void led_off(uint8_t index)
{
    if (index > 8) return;

    if (led_ports[index] == 0)
        PORTB &= ~(1 << led_pins[index]);
    else
        PORTC &= ~(1 << led_pins[index]);
}

void all_leds_off(void)
{
    PORTB &= ~((1 << PB0) | (1 << PB1) | (1 << PB2) |
               (1 << PB3) | (1 << PB4) | (1 << PB5));

    PORTC &= ~((1 << PC0) | (1 << PC1) | (1 << PC2));
}

void show_led_mask(uint16_t mask)
{
    all_leds_off();

    for (uint8_t i = 0; i < 9; i++) {
        if (mask & (1 << i)) {
            led_on(i);
        }
    }
}

/* ================= BUTTONS ================= */

void buttons_init(void)
{
    DDRD &= ~((1 << BTN_TOP) | (1 << BTN_BOTTOM));
    PORTD |= (1 << BTN_TOP) | (1 << BTN_BOTTOM);
}

uint8_t top_pressed(void)
{
    if (!(PIND & (1 << BTN_TOP))) {
        _delay_ms(40);

        if (!(PIND & (1 << BTN_TOP))) {
            while (!(PIND & (1 << BTN_TOP)));

            sound_menu();
            randomize_from_user();

            return 1;
        }
    }

    return 0;
}

uint8_t bottom_pressed(void)
{
    if (!(PIND & (1 << BTN_BOTTOM))) {
        _delay_ms(40);

        if (!(PIND & (1 << BTN_BOTTOM))) {
            while (!(PIND & (1 << BTN_BOTTOM)));

            sound_menu();
            randomize_from_user();

            return 1;
        }
    }

    return 0;
}

/* ================= MATRIX ================= */

void matrix_init(void)
{
    DDRD |= (1 << PD2) | (1 << PD3) | (1 << PD4);

    DDRD &= ~((1 << PD5) | (1 << PD6) | (1 << PD7));
    PORTD |= (1 << PD5) | (1 << PD6) | (1 << PD7);

    PORTD |= (1 << PD2) | (1 << PD3) | (1 << PD4);
}

uint8_t matrix_read_raw(void)
{
    for (uint8_t r = 0; r < 3; r++) {
        PORTD |= (1 << PD2) | (1 << PD3) | (1 << PD4);
        PORTD &= ~(1 << row_pins[r]);

        _delay_us(50);

        for (uint8_t c = 0; c < 3; c++) {
            if (!(PIND & (1 << col_pins[c]))) {
                return r * 3 + c;
            }
        }
    }

    return 255;
}

uint8_t matrix_wait_press(void)
{
    uint8_t b;

    while (1) {
        b = matrix_read_raw();

        if (b != 255) {
            _delay_ms(40);

            if (matrix_read_raw() == b) {
                while (matrix_read_raw() != 255);

                sound_matrix();
                randomize_from_user();

                return b;
            }
        }

        if (bottom_pressed()) return 251;
    }
}

/* ================= UI ================= */

void show_menu(void)
{
    LCD_clear();
    LCD_print_line(0, "1.Sequence", '1');
    LCD_print_line(1, "2.Visual", '2');
}

void show_highscore(void)
{
    LCD_clear();

    if (selected_game == 1) {
        LCD_set_cursor(0, 0);
        LCD_print("Seq HS: ");
        LCD_print_uint(highscore_sequence);
        LCD_set_cursor(0, 15);
        LCD_data('>');
    } else {
        LCD_set_cursor(0, 0);
        LCD_print("Vis HS: ");
        LCD_print_uint(highscore_visual);
        LCD_set_cursor(0, 15);
        LCD_data('>');
    }

    LCD_print_line(1, "Back", '<');
}

void show_score(uint16_t score, uint16_t high)
{
    LCD_clear();

    LCD_set_cursor(0, 0);
    LCD_print("Score: ");
    LCD_print_uint(score);

    LCD_set_cursor(1, 0);
    LCD_print("High: ");
    LCD_print_uint(high);

    LCD_set_cursor(1, 15);
    LCD_data('<');
}

uint8_t game_over_screen(uint16_t score)
{
    LCD_clear();

    LCD_set_cursor(0, 0);
    LCD_print("Game Over");
    LCD_set_cursor(0, 15);
    LCD_data('R');

    LCD_set_cursor(1, 0);
    LCD_print("Score: ");
    LCD_print_uint(score);
    LCD_set_cursor(1, 15);
    LCD_data('<');

    sound_gameover();

    while (1) {
        if (top_pressed()) return 1;
        if (bottom_pressed()) return 0;
    }
}

/* ================= GAME 1: SEQUENCE ================= */

void game_sequence(void)
{
restart_game:
    ;

    uint8_t sequence[40];
    uint8_t length = 1;
    uint16_t score = 0;

    all_leds_off();

    while (1) {
        show_score(score, highscore_sequence);

        sequence[length - 1] = rand() % 9;

        _delay_ms(600);

        for (uint8_t i = 0; i < length; i++) {
            led_on(sequence[i]);
            beep(1300, 80);
            _delay_ms(350);
            led_off(sequence[i]);
            _delay_ms(180);
        }

        for (uint8_t i = 0; i < length; i++) {
            uint8_t input = matrix_wait_press();

            if (input == 251) {
                all_leds_off();
                return;
            }

            if (input != sequence[i]) {
                sound_error();

                if (score > highscore_sequence)
                    highscore_sequence = score;

                all_leds_off();

                if (game_over_screen(score)) {
                    goto restart_game;
                } else {
                    return;
                }
            }
        }

        score++;

        if (score > highscore_sequence)
            highscore_sequence = score;

        sound_success();

        if (length < 40)
            length++;
    }
}

/* ================= GAME 2: VISUAL ================= */

uint16_t generate_visual_mask(uint8_t count)
{
    uint16_t mask = 0;
    uint8_t chosen = 0;

    while (chosen < count) {
        uint8_t b = rand() % 9;

        if (!(mask & (1 << b))) {
            mask |= (1 << b);
            chosen++;
        }
    }

    return mask;
}

void show_visual_input_screen(void)
{
    LCD_clear();

    LCD_set_cursor(0, 0);
    LCD_print("Select LEDs");
    LCD_set_cursor(0, 15);
    LCD_data('>');

    LCD_set_cursor(1, 15);
    LCD_data('<');
}

void game_visual(void)
{
restart_game:
    ;

    uint16_t score = 0;
    uint16_t display_time = 1000;

    all_leds_off();

    while (1) {
        show_score(score, highscore_visual);

        uint8_t led_count = 5 + (rand() % 3);
        uint16_t pattern = generate_visual_mask(led_count);
        uint16_t pressed = 0;

        _delay_ms(700);

        show_led_mask(pattern);

        for (uint16_t t = 0; t < display_time; t += 20) {
            _delay_ms(20);
        }

        all_leds_off();

        show_visual_input_screen();

        while (1) {
            uint8_t input = matrix_read_raw();

            if (input != 255) {
                _delay_ms(40);

                if (matrix_read_raw() == input) {
                    pressed ^= (1 << input);

                    if (pressed & (1 << input)) {
                        led_on(input);
                    } else {
                        led_off(input);
                    }

                    sound_matrix();

                    while (matrix_read_raw() != 255);
                    _delay_ms(100);
                }
            }

            if (top_pressed()) {
                if (pressed == pattern) {
                    score++;

                    if (score > highscore_visual)
                        highscore_visual = score;

                    sound_success();
                    all_leds_off();

                    if (display_time == 1000) {
                        display_time = 750;
                    }
                    else if (display_time == 750) {
                        display_time = 500;
                    }
                    else if (display_time > 300) {
                        display_time -= 100;
                    }
                    else if (display_time > 100) {
                        display_time -= 50;
                    }
                    else if (display_time > 10) {
                        display_time -= 10;
                    }

                    break;
                } else {
                    sound_error();

                    if (score > highscore_visual)
                        highscore_visual = score;

                    all_leds_off();

                    if (game_over_screen(score)) {
                        goto restart_game;
                    } else {
                        return;
                    }
                }
            }

            if (bottom_pressed()) {
                all_leds_off();
                return;
            }
        }
    }
}

/* ================= MAIN ================= */

int main(void)
{
    buttons_init();
    matrix_init();
    leds_init();
    buzzer_init();

    TWI_init();
    LCD_init();

    seed_random();

    all_leds_off();
    show_menu();

    while (1) {
        if (top_pressed()) {
            selected_game = 1;
            show_highscore();

            while (1) {
                if (top_pressed()) {
                    game_sequence();
                    show_highscore();
                }

                if (bottom_pressed()) {
                    show_menu();
                    break;
                }
            }
        }

        if (bottom_pressed()) {
            selected_game = 2;
            show_highscore();

            while (1) {
                if (top_pressed()) {
                    game_visual();
                    show_highscore();
                }

                if (bottom_pressed()) {
                    show_menu();
                    break;
                }
            }
        }
    }

    return 0;
}