/*
 * Jocuri Interactive de Memorie
 * Firmware pentru ATmega328P scris in AVR C.
 *
 * Functionalitati:
 *  - LCD 1602 prin I2C/TWI
 *  - matrice 3x3 de butoane
 *  - 9 LED-uri controlate individual
 *  - 2 butoane de meniu
 *  - buzzer pasiv
 *  - joc Sequence Memory
 *  - joc Visual Memory
 */

#ifndef F_CPU
/* Frecventa microcontrollerului. Este necesara pentru _delay_ms/_delay_us. */
#define F_CPU 16000000UL
#endif

/* Biblioteca principala AVR: registre DDRx, PORTx, PINx, TWI etc. */
#include <avr/io.h>

/* Delay-uri software. */
#include <util/delay.h>

/* Tipuri cu dimensiune fixa: uint8_t, uint16_t etc. */
#include <stdint.h>

/* rand(), srand(), itoa() */
#include <stdlib.h>

/* ================= CONSTANTE LCD ================= */

/* Adresa I2C a modulului LCD. La majoritatea modulelor este 0x27 sau 0x3F. */
#define LCD_ADDR 0x27

/* Biti folositi de expanderul I2C al LCD-ului. */
#define LCD_BACKLIGHT 0x08  /* tine backlight-ul pornit */
#define LCD_ENABLE    0x04  /* semnalul Enable al LCD-ului */
#define LCD_RS        0x01  /* Register Select: 0 = comanda, 1 = data */

/* ================= PINI BUTOANE MENIU SI BUZZER ================= */

/*
 * Cele doua butoane de meniu sunt conectate pe PD0 si PD1.
 * Sunt citite cu INPUT_PULLUP, deci:
 *  - neapasat = HIGH
 *  - apasat   = LOW
 */
#define BTN_TOP     PD0
#define BTN_BOTTOM  PD1

/* Buzzerul pasiv este conectat la PC3. */
#define BUZZER PC3

/* ================= MATRICE BUTOANE ================= */

/*
 * Matricea 3x3 foloseste 3 randuri si 3 coloane.
 * Randurile sunt OUTPUT, coloanele sunt INPUT_PULLUP.
 */
uint8_t row_pins[3] = { PD2, PD3, PD4 };
uint8_t col_pins[3] = { PD5, PD6, PD7 };

/* ================= MAPARE LED-URI ================= */

/*
 * LED-urile sunt controlate individual.
 *
 * Primele 6 LED-uri sunt pe PORTB:
 *  LED 0 -> PB0
 *  LED 1 -> PB1
 *  ...
 *  LED 5 -> PB5
 *
 * Ultimele 3 LED-uri sunt pe PORTC:
 *  LED 6 -> PC0
 *  LED 7 -> PC1
 *  LED 8 -> PC2
 *
 * led_ports:
 *  0 = PORTB
 *  1 = PORTC
 */
uint8_t led_ports[9] = {0,0,0,0,0,0,1,1,1};
uint8_t led_pins[9]  = {PB0,PB1,PB2,PB3,PB4,PB5,PC0,PC1,PC2};

/* Jocul selectat curent: 1 = Sequence, 2 = Visual. */
uint8_t selected_game = 1;

/* Highscore-urile sunt pastrate in RAM. Se reseteaza la oprirea placii. */
uint16_t highscore_sequence = 0;
uint16_t highscore_visual = 0;

/* ================= I2C / TWI ================= */

/*
 * Initializeaza perifericul TWI/I2C al ATmega328P.
 *
 * PC4 = SDA
 * PC5 = SCL
 *
 * PORTC activeaza pull-up intern pe SDA/SCL.
 * TWBR = 72 seteaza frecventa I2C aproape de 100 kHz la F_CPU = 16 MHz.
 */
void TWI_init(void)
{
    PORTC |= (1 << PC4) | (1 << PC5);
    TWSR = 0x00;
    TWBR = 72;
}

/* Genereaza conditia START pe magistrala I2C. */
void TWI_start(void)
{
    TWCR = (1 << TWINT) | (1 << TWSTA) | (1 << TWEN);

    /* Asteapta terminarea operatiei. */
    while (!(TWCR & (1 << TWINT)));
}

/* Genereaza conditia STOP pe magistrala I2C. */
void TWI_stop(void)
{
    TWCR = (1 << TWINT) | (1 << TWSTO) | (1 << TWEN);
    _delay_us(20);
}

/* Trimite un octet pe magistrala I2C. */
void TWI_write(uint8_t data)
{
    TWDR = data;
    TWCR = (1 << TWINT) | (1 << TWEN);

    /* Asteapta pana cand transmisia este terminata. */
    while (!(TWCR & (1 << TWINT)));
}

/* ================= LCD ================= */

/*
 * Trimite un octet catre expanderul I2C al LCD-ului.
 * Se trimite si bitul LCD_BACKLIGHT pentru a mentine lumina aprinsa.
 */
void LCD_expander_write(uint8_t data)
{
    TWI_start();

    /* Adresa I2C se trimite deplasata cu 1 bit la stanga. Bitul 0 este R/W. */
    TWI_write(LCD_ADDR << 1);

    /* Trimite datele catre expander. */
    TWI_write(data | LCD_BACKLIGHT);

    TWI_stop();
}

/*
 * Creeaza un impuls pe pinul Enable al LCD-ului.
 * LCD-ul citeste datele atunci cand Enable este pulsata.
 */
void LCD_pulse_enable(uint8_t data)
{
    LCD_expander_write(data | LCD_ENABLE);
    _delay_us(2);

    LCD_expander_write(data & ~LCD_ENABLE);
    _delay_us(100);
}

/* Trimite 4 biti catre LCD, prin expanderul I2C. */
void LCD_write4(uint8_t data)
{
    LCD_expander_write(data);
    LCD_pulse_enable(data);
}

/*
 * Trimite un octet catre LCD in modul 4-bit.
 * Octetul se imparte in doua jumatati:
 *  - high nibble
 *  - low nibble
 *
 * mode = 0      -> comanda
 * mode = LCD_RS -> caracter de afisat
 */
void LCD_send(uint8_t value, uint8_t mode)
{
    LCD_write4((value & 0xF0) | mode);
    LCD_write4(((value << 4) & 0xF0) | mode);
}

/* Trimite o comanda catre LCD. */
void LCD_command(uint8_t cmd)
{
    LCD_send(cmd, 0);
}

/* Trimite un caracter catre LCD. */
void LCD_data(uint8_t data)
{
    LCD_send(data, LCD_RS);
}

/* Sterge ecranul LCD. Comanda are nevoie de cateva milisecunde. */
void LCD_clear(void)
{
    LCD_command(0x01);
    _delay_ms(3);
}

/*
 * Initializeaza LCD-ul in modul 4-bit, 2 randuri.
 * Secventa este cea standard pentru controllere compatibile HD44780.
 */
void LCD_init(void)
{
    _delay_ms(100);

    /* Secventa de initializare in 8-bit fallback. */
    LCD_write4(0x30);
    _delay_ms(10);
    LCD_write4(0x30);
    _delay_ms(10);
    LCD_write4(0x30);
    _delay_ms(10);

    /* Trecere la modul 4-bit. */
    LCD_write4(0x20);
    _delay_ms(10);

    LCD_command(0x28); /* 4-bit, 2 linii, font 5x8 */
    LCD_command(0x08); /* display off */
    LCD_clear();
    LCD_command(0x06); /* cursor increment */
    LCD_command(0x0C); /* display on, cursor off */
}

/* Seteaza pozitia cursorului pe LCD. row = 0/1, col = 0..15. */
void LCD_set_cursor(uint8_t row, uint8_t col)
{
    uint8_t addr = (row == 0) ? col : (0x40 + col);
    LCD_command(0x80 | addr);
}

/* Afiseaza un string pe LCD. */
void LCD_print(const char *str)
{
    while (*str) {
        LCD_data(*str++);
    }
}

/* Afiseaza un numar unsigned pe LCD. */
void LCD_print_uint(uint16_t x)
{
    char buf[6];
    itoa(x, buf, 10);
    LCD_print(buf);
}

/*
 * Afiseaza un rand complet pe LCD.
 * Primele 15 coloane sunt textul, iar coloana 15 este caracterul actiunii.
 * Ex: '>', '<', 'R', '1', '2'
 */
void LCD_print_line(uint8_t row, const char *text, char action)
{
    LCD_set_cursor(row, 0);

    uint8_t i = 0;

    /* Scrie textul, dar nu depaseste coloana 14. */
    while (text[i] && i < 15) {
        LCD_data(text[i]);
        i++;
    }

    /* Completeaza restul randului cu spatii. */
    while (i < 15) {
        LCD_data(' ');
        i++;
    }

    /* Ultima coloana arata ce face butonul corespunzator randului. */
    LCD_data(action);
}

/* ================= RANDOM ================= */

/*
 * Initializeaza generatorul pseudo-random.
 *
 * Se foloseste Timer0 si starea pinilor pentru a obtine un seed variabil.
 * Nu este random criptografic, dar este suficient pentru joc.
 */
void seed_random(void)
{
    /* Porneste Timer0 cu prescaler 1024. */
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

/*
 * Adauga entropie suplimentara dupa apasarile utilizatorului.
 * Timing-ul uman produce mici diferente utile pentru randomizare.
 */
void randomize_from_user(void)
{
    uint16_t seed = rand();
    seed ^= TCNT0;
    seed ^= (PIND << 3);
    srand(seed);
}

/* ================= BUZZER ================= */

/* Configureaza pinul buzzerului ca iesire. */
void buzzer_init(void)
{
    DDRC |= (1 << BUZZER);
    PORTC &= ~(1 << BUZZER);
}

/*
 * Delay aproximativ pentru generarea sunetului.
 * Pentru frecvente diferite se folosesc delay-uri diferite.
 */
void buzz_delay(uint16_t freq)
{
    if (freq <= 300) _delay_us(1600);
    else if (freq <= 600) _delay_us(800);
    else if (freq <= 1000) _delay_us(500);
    else if (freq <= 1500) _delay_us(330);
    else _delay_us(250);
}

/*
 * Genereaza un beep pe buzzer prin toggling manual al pinului.
 * Nu foloseste PWM hardware, ci comuta pinul HIGH/LOW rapid.
 */
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

/* Sunete diferite pentru evenimente diferite. */
void sound_menu(void)      { beep(900, 40); }
void sound_matrix(void)    { beep(1200, 40); }
void sound_success(void)   { beep(1600, 50); _delay_ms(60); beep(1800, 50); }
void sound_error(void)     { beep(250, 250); }
void sound_gameover(void)  { beep(400, 120); _delay_ms(80); beep(250, 180); }

/* ================= LEDS ================= */

/* Configureaza toti pinii LED-urilor ca iesiri. */
void leds_init(void)
{
    DDRB |= (1 << PB0) | (1 << PB1) | (1 << PB2) |
            (1 << PB3) | (1 << PB4) | (1 << PB5);

    DDRC |= (1 << PC0) | (1 << PC1) | (1 << PC2);
}

/* Aprinde LED-ul cu indexul 0..8. */
void led_on(uint8_t index)
{
    if (index > 8) return;

    if (led_ports[index] == 0) {
        PORTB |= (1 << led_pins[index]);
    } else {
        PORTC |= (1 << led_pins[index]);
    }
}

/* Stinge LED-ul cu indexul 0..8. */
void led_off(uint8_t index)
{
    if (index > 8) return;

    if (led_ports[index] == 0) {
        PORTB &= ~(1 << led_pins[index]);
    } else {
        PORTC &= ~(1 << led_pins[index]);
    }
}

/* Stinge toate LED-urile din matrice. */
void all_leds_off(void)
{
    PORTB &= ~((1 << PB0) | (1 << PB1) | (1 << PB2) |
               (1 << PB3) | (1 << PB4) | (1 << PB5));

    PORTC &= ~((1 << PC0) | (1 << PC1) | (1 << PC2));
}

/*
 * Aprinde un pattern de LED-uri reprezentat prin masca de biti.
 * Bitul 0 -> LED 0
 * Bitul 1 -> LED 1
 * ...
 * Bitul 8 -> LED 8
 */
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

/*
 * Configureaza butoanele de meniu ca input pull-up.
 * La apasare, pinul este conectat la GND si devine LOW.
 */
void buttons_init(void)
{
    DDRD &= ~((1 << BTN_TOP) | (1 << BTN_BOTTOM));
    PORTD |= (1 << BTN_TOP) | (1 << BTN_BOTTOM);
}

/* Detecteaza apasarea butonului de sus. Returneaza 1 la apasare valida. */
uint8_t top_pressed(void)
{
    if (!(PIND & (1 << BTN_TOP))) {
        _delay_ms(40); /* debounce */

        if (!(PIND & (1 << BTN_TOP))) {
            /* Asteapta eliberarea butonului ca sa nu detecteze de mai multe ori. */
            while (!(PIND & (1 << BTN_TOP)));

            sound_menu();
            randomize_from_user();

            return 1;
        }
    }

    return 0;
}

/* Detecteaza apasarea butonului de jos. Returneaza 1 la apasare valida. */
uint8_t bottom_pressed(void)
{
    if (!(PIND & (1 << BTN_BOTTOM))) {
        _delay_ms(40); /* debounce */

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

/*
 * Initializeaza matricea de butoane.
 *
 * Randuri:
 *  PD2, PD3, PD4 -> OUTPUT
 *
 * Coloane:
 *  PD5, PD6, PD7 -> INPUT_PULLUP
 */
void matrix_init(void)
{
    DDRD |= (1 << PD2) | (1 << PD3) | (1 << PD4);

    DDRD &= ~((1 << PD5) | (1 << PD6) | (1 << PD7));
    PORTD |= (1 << PD5) | (1 << PD6) | (1 << PD7);

    /* Initial, toate randurile sunt HIGH. */
    PORTD |= (1 << PD2) | (1 << PD3) | (1 << PD4);
}

/*
 * Citeste matricea o singura data.
 *
 * Algoritm:
 *  1. Pune toate randurile HIGH.
 *  2. Pune randul curent LOW.
 *  3. Citeste coloanele.
 *  4. Daca o coloana este LOW, butonul de la intersectie este apasat.
 *
 * Returneaza:
 *  0..8  -> index buton
 *  255   -> niciun buton apasat
 */
uint8_t matrix_read_raw(void)
{
    for (uint8_t r = 0; r < 3; r++) {
        PORTD |= (1 << PD2) | (1 << PD3) | (1 << PD4);
        PORTD &= ~(1 << row_pins[r]);

        _delay_us(50); /* timp scurt pentru stabilizarea semnalului */

        for (uint8_t c = 0; c < 3; c++) {
            if (!(PIND & (1 << col_pins[c]))) {
                return r * 3 + c;
            }
        }
    }

    return 255;
}

/*
 * Asteapta pana cand este apasat un buton din matrice.
 *
 * Returneaza:
 *  0..8  -> butonul apasat
 *  251   -> butonul de back a fost apasat
 */
uint8_t matrix_wait_press(void)
{
    uint8_t b;

    while (1) {
        b = matrix_read_raw();

        if (b != 255) {
            _delay_ms(40); /* debounce */

            if (matrix_read_raw() == b) {
                while (matrix_read_raw() != 255);

                sound_matrix();
                randomize_from_user();

                return b;
            }
        }

        /* In timpul jocului, butonul de jos functioneaza ca Back. */
        if (bottom_pressed()) return 251;
    }
}

/* ================= UI ================= */

/* Afiseaza meniul principal. */
void show_menu(void)
{
    LCD_clear();
    LCD_print_line(0, "1.Sequence", '1');
    LCD_print_line(1, "2.Visual", '2');
}

/* Afiseaza highscore-ul jocului selectat. */
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

/* Afiseaza scorul curent si highscore-ul in timpul jocului. */
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

/*
 * Afiseaza ecranul de Game Over.
 *
 * Buton sus  -> Restart
 * Buton jos  -> Back
 */
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

/*
 * Jocul Sequence Memory.
 *
 * Sistemul genereaza o secventa de butoane.
 * La fiecare runda se adauga un nou element random.
 * Utilizatorul trebuie sa reproduca intreaga secventa in ordine.
 */
void game_sequence(void)
{
restart_game:
    ;

    uint8_t sequence[40];  /* vectorul in care este stocata secventa */
    uint8_t length = 1;    /* lungimea curenta a secventei */
    uint16_t score = 0;    /* scorul curent */

    all_leds_off();

    while (1) {
        show_score(score, highscore_sequence);

        /* Adauga un nou buton random la finalul secventei. */
        sequence[length - 1] = rand() % 9;

        _delay_ms(600);

        /* Afiseaza secventa prin aprinderea LED-urilor in ordine. */
        for (uint8_t i = 0; i < length; i++) {
            led_on(sequence[i]);
            beep(1300, 80);

            _delay_ms(350);

            led_off(sequence[i]);
            _delay_ms(180);
        }

        /* Citeste raspunsul utilizatorului si compara cu secventa. */
        for (uint8_t i = 0; i < length; i++) {
            uint8_t input = matrix_wait_press();

            /* Back catre meniul anterior. */
            if (input == 251) {
                all_leds_off();
                return;
            }

            /* Daca inputul este gresit, jocul se termina. */
            if (input != sequence[i]) {
                sound_error();

                if (score > highscore_sequence) {
                    highscore_sequence = score;
                }

                all_leds_off();

                if (game_over_screen(score)) {
                    goto restart_game;
                } else {
                    return;
                }
            }
        }

        /* Runda corecta. */
        score++;

        if (score > highscore_sequence) {
            highscore_sequence = score;
        }

        sound_success();

        /* Creste lungimea secventei. */
        if (length < 40) {
            length++;
        }
    }
}

/* ================= GAME 2: VISUAL ================= */

/*
 * Genereaza o masca de biti cu un numar dat de LED-uri aprinse.
 * Fiecare bit din masca reprezinta un LED.
 */
uint16_t generate_visual_mask(uint8_t count)
{
    uint16_t mask = 0;
    uint8_t chosen = 0;

    while (chosen < count) {
        uint8_t b = rand() % 9;

        /* Evita duplicatele: LED-ul este adaugat doar daca nu exista deja. */
        if (!(mask & (1 << b))) {
            mask |= (1 << b);
            chosen++;
        }
    }

    return mask;
}

/* Afiseaza ecranul in care utilizatorul selecteaza LED-urile memorate. */
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

/*
 * Jocul Visual Memory.
 *
 * Sistemul aprinde simultan 5-7 LED-uri pentru un timp limitat.
 * Utilizatorul trebuie sa selecteze aceleasi LED-uri.
 * Confirmarea se face cu butonul de sus.
 */
void game_visual(void)
{
restart_game:
    ;

    uint16_t score = 0;
    uint16_t display_time = 1000; /* timpul initial de afisare in ms */

    all_leds_off();

    while (1) {
        show_score(score, highscore_visual);

        /* Alege random intre 5 si 7 LED-uri. */
        uint8_t led_count = 5 + (rand() % 3);

        /* Pattern-ul corect generat de sistem. */
        uint16_t pattern = generate_visual_mask(led_count);

        /* Selectia utilizatorului. */
        uint16_t pressed = 0;

        _delay_ms(700);

        /* Afiseaza pattern-ul pentru un timp limitat. */
        show_led_mask(pattern);

        for (uint16_t t = 0; t < display_time; t += 20) {
            _delay_ms(20);
        }

        all_leds_off();

        show_visual_input_screen();

        while (1) {
            uint8_t input = matrix_read_raw();

            /* Daca utilizatorul apasa un buton din matrice, acesta este toggle-uit. */
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

            /* Butonul de sus confirma raspunsul. */
            if (top_pressed()) {
                if (pressed == pattern) {
                    score++;

                    if (score > highscore_visual) {
                        highscore_visual = score;
                    }

                    sound_success();
                    all_leds_off();

                    /*
                     * Scade timpul de afisare pentru cresterea dificultatii.
                     * Progresie:
                     * 1000 -> 750 -> 500 -> 400 -> 300 -> 250 -> ...
                     */
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

                    if (score > highscore_visual) {
                        highscore_visual = score;
                    }

                    all_leds_off();

                    if (game_over_screen(score)) {
                        goto restart_game;
                    } else {
                        return;
                    }
                }
            }

            /* Butonul de jos intoarce utilizatorul la meniul anterior. */
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
    /* Initializare periferice si module software. */
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
        /*
         * In meniul principal:
         *  - buton sus  -> Sequence Memory
         *  - buton jos  -> Visual Memory
         */
        if (top_pressed()) {
            selected_game = 1;
            show_highscore();

            while (1) {
                /* Buton sus porneste jocul selectat. */
                if (top_pressed()) {
                    game_sequence();
                    show_highscore();
                }

                /* Buton jos revine la meniul principal. */
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
