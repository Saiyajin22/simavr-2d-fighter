#undef F_CPU
#define F_CPU 16000000
#include "avr_mcu_section.h"
AVR_MCU(F_CPU, "atmega128");

#define __AVR_ATmega128__1
#include <avr/io.h>
#include <util/delay.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

static void port_init()
{
    PORTA = 0b00011111;
    DDRA = 0b01000000; // buttons & led
    PORTB = 0b00000000;
    DDRB = 0b00000000;
    PORTC = 0b00000000;
    DDRC = 0b11110111; // lcd
    PORTD = 0b11000000;
    DDRD = 0b00001000;
    PORTE = 0b00100000;
    DDRE = 0b00110000; // buzzer
    PORTF = 0b00000000;
    DDRF = 0b00000000;
    PORTG = 0b00000000;
    DDRG = 0b00000000;
}

// BUTTON HANDLING -----------------------------------------------------------

#define BUTTON_NONE 0
#define BUTTON_CENTER 1
#define BUTTON_LEFT 2
#define BUTTON_RIGHT 3
#define BUTTON_UP 4
#define BUTTON_DOWN 5
static int button_accept = 1;

static int button_pressed()
{
    // right
    if (!(PINA & 0b00000001) & button_accept)
    {                      // check state of button 1 and value of button_accept
        button_accept = 0; // button is pressed
        return BUTTON_RIGHT;
    }

    // up
    if (!(PINA & 0b00000010) & button_accept)
    {                      // check state of button 2 and value of button_accept
        button_accept = 0; // button is pressed
        return BUTTON_UP;
    }

    // center
    if (!(PINA & 0b00000100) & button_accept)
    {                      // check state of button 3 and value of button_accept
        button_accept = 0; // button is pressed
        return BUTTON_CENTER;
    }

    // left
    if (!(PINA & 0b00010000) & button_accept)
    {                      // check state of button 5 and value of button_accept
        button_accept = 0; // button is pressed
        return BUTTON_LEFT;
    }

    // down
    if (!(PINA & 0b00001000) & button_accept)
    {                      // check state of button 4 and value of button_accept
        button_accept = 0; // button is pressed
        return BUTTON_DOWN;
    }

    return BUTTON_NONE;
}

static void button_unlock()
{
    // check state of all buttons
    if (
        ((PINA & 0b00000001) | (PINA & 0b00000010) | (PINA & 0b00000100) | (PINA & 0b00001000) | (PINA & 0b00010000)) == 31)
        button_accept = 1; // if all buttons are released button_accept gets value 1
}

// LCD HELPERS ---------------------------------------------------------------

#define CLR_DISP 0x00000001
#define DISP_ON 0x0000000C
#define DISP_OFF 0x00000008
#define CUR_HOME 0x00000002
#define CUR_OFF 0x0000000C
#define CUR_ON_UNDER 0x0000000E
#define CUR_ON_BLINK 0x0000000F
#define CUR_LEFT 0x00000010
#define CUR_RIGHT 0x00000014
#define CG_RAM_ADDR 0x00000040
#define DD_RAM_ADDR 0x00000080
#define DD_RAM_ADDR2 0x000000C0

// #define ENTRY_INC 0x00000007 //LCD increment
// #define ENTRY_DEC 0x00000005 //LCD decrement
// #define SH_LCD_LEFT 0x00000010 //LCD shift left
// #define SH_LCD_RIGHT 0x00000014 //LCD shift right
// #define MV_LCD_LEFT 0x00000018 //LCD move left
// #define MV_LCD_RIGHT 0x0000001C //LCD move right

static void lcd_delay(unsigned int b)
{
    volatile unsigned int a = b;
    while (a)
        a--;
}

static void lcd_pulse()
{
    PORTC = PORTC | 0b00000100; // set E to high
    lcd_delay(1400);            // delay ~110ms
    PORTC = PORTC & 0b11111011; // set E to low
}

static void lcd_send(int command, unsigned char a)
{
    unsigned char data;

    data = 0b00001111 | a;               // get high 4 bits
    PORTC = (PORTC | 0b11110000) & data; // set D4-D7
    if (command)
        PORTC = PORTC & 0b11111110; // set RS port to 0 -> display set to command mode
    else
        PORTC = PORTC | 0b00000001; // set RS port to 1 -> display set to data mode
    lcd_pulse();                    // pulse to set D4-D7 bits

    data = a << 4;                       // get low 4 bits
    PORTC = (PORTC & 0b00001111) | data; // set D4-D7
    if (command)
        PORTC = PORTC & 0b11111110; // set RS port to 0 -> display set to command mode
    else
        PORTC = PORTC | 0b00000001; // set RS port to 1 -> display set to data mode
    lcd_pulse();                    // pulse to set d4-d7 bits
}

static void lcd_send_command(unsigned char a)
{
    lcd_send(1, a);
}

static void lcd_send_data(unsigned char a)
{
    lcd_send(0, a);
}

static void lcd_init()
{
    // LCD initialization
    // step by step (from Gosho) - from DATASHEET

    PORTC = PORTC & 0b11111110;

    lcd_delay(10000);

    PORTC = 0b00110000; // set D4, D5 port to 1
    lcd_pulse();        // high->low to E port (pulse)
    lcd_delay(1000);

    PORTC = 0b00110000; // set D4, D5 port to 1
    lcd_pulse();        // high->low to E port (pulse)
    lcd_delay(1000);

    PORTC = 0b00110000; // set D4, D5 port to 1
    lcd_pulse();        // high->low to E port (pulse)
    lcd_delay(1000);

    PORTC = 0b00100000; // set D4 to 0, D5 port to 1
    lcd_pulse();        // high->low to E port (pulse)

    lcd_send_command(0x28);     // function set: 4 bits interface, 2 display lines, 5x8 font
    lcd_send_command(DISP_OFF); // display off, cursor off, blinking off
    lcd_send_command(CLR_DISP); // clear display
    lcd_send_command(0x06);     // entry mode set: cursor increments, display does not shift

    lcd_send_command(DISP_ON);  // Turn ON Display
    lcd_send_command(CLR_DISP); // Clear Display
}

static void lcd_send_text(char *str)
{
    while (*str)
        lcd_send_data(*str++);
}

static void lcd_send_line1(char *str)
{
    lcd_send_command(DD_RAM_ADDR);
    lcd_send_text(str);
}

static void lcd_send_line2(char *str)
{
    lcd_send_command(DD_RAM_ADDR2);
    lcd_send_text(str);
}

static void clearDisplay()
{
    lcd_send_command(CLR_DISP);
}

// CUSTOM CHARACTERS ------------------------------

static unsigned char CUSTOM_CHARACTERS[8][8] = {
    {0b01110,
     0b00110,
     0b01110,
     0b00100,
     0b11111,
     0b00100,
     0b01110,
     0b01010}, // 0 - Player Looking left
    {
        0b01110,
        0b01100,
        0b01110,
        0b00100,
        0b11111,
        0b00100,
        0b01110,
        0b01010}, // 1 - Player looking right
    {
        0b11110,
        0b11110,
        0b11010,
        0b11010,
        0b00010,
        0b00010,
        0b00010,
        0b00010}, // 2 - Player attack left
    {
        0b01111,
        0b01111,
        0b01011,
        0b01011,
        0b01000,
        0b01000,
        0b01000,
        0b01000}, // 3 - Player attack right
    {
        0b11100,
        0b11000,
        0b01011,
        0b01011,
        0b01110,
        0b01010,
        0b01000,
        0b10100}, // 4 - Enemy coming from left
    {
        0b00111,
        0b00011,
        0b11010,
        0b11010,
        0b01110,
        0b01010,
        0b00010,
        0b00101}, // 5 - Enemy coming from right
    {
        0b00000,
        0b00000,
        0b11111,
        0b00011,
        0b00011,
        0b00000,
        0b00000,
        0b00000}, // 6 - Enemy attack from left
    {
        0b00000,
        0b00000,
        0b11111,
        0b11000,
        0b11000,
        0b00000,
        0b00000,
        0b00000}, // 7 - Enemy attack from right
};

static void chars_init()
{
    for (int c = 0; c < 8; ++c)
    {
        lcd_send_command(CG_RAM_ADDR + c * 8);
        for (int r = 0; r < 8; ++r)
            lcd_send_data(CUSTOM_CHARACTERS[c][r]);
    }
}

// HELPER FUNCTIONS ---------------------------------------

void wait(volatile int num1, volatile int num2)
{
    for (volatile int i = 0; i < num1; i++)
    {
        for (volatile int j = 0; j < num2; j++)
        {
        }
    }
}

void customSleep(unsigned int seconds)
{
    // Each _delay_ms call sleeps for 1000 milliseconds (1 second)
    while (seconds--)
    {
        _delay_ms(1000);
    }
}

// Generate a random number between 0 and the given range
int randomNumber(int range)
{
    return rand() % (range + 1);
}

// GAME RELATED FUNCTIONS, VARIABLES, ETC... ---------------------------------

int playerCol = 7;
int playerRow = DD_RAM_ADDR2;
int playerRowNum = 1;
int playerScore = 0;
int spawnEnemy = 0;
int DISPLAY_POSITIONS[2][16] = {
    {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0}, // 0 == nothing, 1 == player, 4 == enemy coming left, 5 == enemy coming right
    {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0}};
int PLAYER = 1;
#define PLAYER_ATTACK_LEFT 2
#define PLAYER_ATTACK_RIGHT 3
#define ENEMY_COMING_LEFT 4
#define ENEMY_COMING_RIGHT 5
#define ENEMY_ATTACK_LEFT 6
#define ENEMY_ATTACK_RIGHT 7

void initCharacter()
{
    lcd_send_command(playerRow + playerCol);
    lcd_send_data(PLAYER);
}

int validatePlayerPosition()
{
    if (playerCol >= 0 && playerCol <= 15)
        return 1;
    return 0;
}

int isPlayerDead()
{
    if (DISPLAY_POSITIONS[playerRowNum][playerCol-1] == ENEMY_COMING_LEFT || DISPLAY_POSITIONS[playerRowNum][playerCol+1] == ENEMY_COMING_RIGHT) {
        if (DISPLAY_POSITIONS[playerRowNum][playerCol-1] == ENEMY_COMING_LEFT) {
            lcd_send_command(playerRow + playerCol);
            lcd_send_data(ENEMY_ATTACK_LEFT);
        } else {
            lcd_send_command(playerRow + playerCol);
            lcd_send_data(ENEMY_ATTACK_RIGHT);
        }
            
        return 1;
    }
        
    return 0;
}

// TODO REFINE MOVEMENT, MAKE IT MORE RANDOM
void enemyMovement()
{
    int NEW_DISPLAY_POSITIONS[2][16] = {
        {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
        {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0}};
    for (int i = 0; i < 2; ++i)
    {
        for (int j = 0; j < 16; ++j)
        {
            if (DISPLAY_POSITIONS[i][j] == ENEMY_COMING_LEFT)
            {
                if (j != 15)
                {
                    NEW_DISPLAY_POSITIONS[i][j + 1] = ENEMY_COMING_LEFT;
                }

                if (i == 1)
                {
                    lcd_send_command(DD_RAM_ADDR2 + j);
                    lcd_send_data(' ');
                    lcd_send_command(DD_RAM_ADDR2 + j + 1);
                    lcd_send_data(ENEMY_COMING_LEFT);
                }
                else
                {
                    lcd_send_command(DD_RAM_ADDR + j);
                    lcd_send_data(' ');
                    lcd_send_command(DD_RAM_ADDR + j + 1);
                    lcd_send_data(ENEMY_COMING_LEFT);
                }
            }
            else if (DISPLAY_POSITIONS[i][j] == ENEMY_COMING_RIGHT)
            {
                if (j != 0)
                {
                    NEW_DISPLAY_POSITIONS[i][j - 1] = ENEMY_COMING_RIGHT;
                }

                if (i == 1)
                {
                    lcd_send_command(DD_RAM_ADDR2 + j);
                    lcd_send_data(' ');
                    lcd_send_command(DD_RAM_ADDR2 + j - 1);
                    lcd_send_data(ENEMY_COMING_RIGHT);
                }
                else
                {
                    lcd_send_command(DD_RAM_ADDR + j);
                    lcd_send_data(' ');
                    lcd_send_command(DD_RAM_ADDR + j - 1);
                    lcd_send_data(ENEMY_COMING_RIGHT);
                }
            }
        }
    }
    for (int i = 0; i < 2; ++i)
    {
        for (int j = 0; j < 16; ++j)
        {
            DISPLAY_POSITIONS[i][j] = NEW_DISPLAY_POSITIONS[i][j];
        }
    }
    wait(12, 32000);
}

// THE GAME -----------------------------------------------

int main()
{
    port_init();
    lcd_init();
    chars_init();
    lcd_send_line1("   2D Fighter");
    lcd_send_line2("Press S to start");

    // welcome screen
    while (1)
    {
        while (button_pressed() != BUTTON_DOWN)
        {
            button_unlock();
        }
        clearDisplay();
        button_unlock();
        break;
    }
    initCharacter();

    // game loop
    while (1)
    {
        int button = button_pressed();

        if (button != BUTTON_NONE)
        {
            spawnEnemy--;
        }
        // Handle enemies
        if (spawnEnemy == 0)
        {
            int leftOrRight = randomNumber(1);
            int upOrDown = randomNumber(1);
            // left enemy spawn
            if (leftOrRight == 0)
            {
                if (upOrDown == 0)
                {
                    lcd_send_command(DD_RAM_ADDR2);
                    lcd_send_data(ENEMY_COMING_LEFT);
                    DISPLAY_POSITIONS[1][0] = ENEMY_COMING_LEFT;
                }
                else
                {
                    lcd_send_command(DD_RAM_ADDR);
                    lcd_send_data(ENEMY_COMING_LEFT);
                    DISPLAY_POSITIONS[0][0] = ENEMY_COMING_LEFT;
                }
            }
            // right enemy spawn
            else
            {
                if (upOrDown == 0)
                {
                    lcd_send_command(DD_RAM_ADDR2 + 15);
                    lcd_send_data(ENEMY_COMING_RIGHT);
                    DISPLAY_POSITIONS[1][15] = ENEMY_COMING_RIGHT;
                }
                else
                {
                    lcd_send_command(DD_RAM_ADDR + 15);
                    lcd_send_data(ENEMY_COMING_RIGHT);
                    DISPLAY_POSITIONS[0][15] = ENEMY_COMING_RIGHT;
                }
            }
            spawnEnemy = randomNumber(8);
        }

        // buttons handling
        if (button == BUTTON_RIGHT)
        {
            playerCol++;
            if (validatePlayerPosition())
            {
                lcd_send_command(playerRow + playerCol - 1);
                lcd_send_data(' ');
                PLAYER = 1;
                lcd_send_command(playerRow + playerCol);
                lcd_send_data(PLAYER);
            }
            else
            {
                playerCol--;
            }
        }
        else if (button == BUTTON_LEFT)
        {
            playerCol--;
            if (validatePlayerPosition())
            {
                lcd_send_command(playerRow + playerCol + 1);
                lcd_send_data(' ');
                PLAYER = 0;
                lcd_send_command(playerRow + playerCol);
                lcd_send_data(PLAYER);
            }
            else
            {
                playerCol++;
            }
        }
        else if (button == BUTTON_UP)
        {
            if (playerRow == DD_RAM_ADDR2)
            {
                lcd_send_command(playerRow + playerCol);
                lcd_send_data(' ');

                playerRow = DD_RAM_ADDR;
                playerRowNum = 0;
                lcd_send_command(playerRow + playerCol);
                lcd_send_data(PLAYER);
            }
        }
        else if (button == BUTTON_DOWN)
        {
            if (playerRow == DD_RAM_ADDR)
            {
                lcd_send_command(playerRow + playerCol);
                lcd_send_data(' ');

                playerRow = DD_RAM_ADDR2;
                playerRowNum = 1;
                lcd_send_command(playerRow + playerCol);
                lcd_send_data(PLAYER);
            }
        }
        // Attack action
        else if (button == BUTTON_CENTER)
        {
            if (PLAYER == 0)
            {
                lcd_send_command(playerRow + playerCol - 1);
                lcd_send_data(PLAYER_ATTACK_LEFT);
                if (DISPLAY_POSITIONS[playerRowNum][playerCol - 1] == ENEMY_COMING_LEFT || DISPLAY_POSITIONS[playerRowNum][playerCol - 2] == ENEMY_COMING_LEFT || DISPLAY_POSITIONS[playerRowNum][playerCol - 3] == ENEMY_COMING_LEFT)
                {
                    DISPLAY_POSITIONS[playerRowNum][playerCol - 1] = 0;
                    DISPLAY_POSITIONS[playerRowNum][playerCol - 2] = 0;
                    DISPLAY_POSITIONS[playerRowNum][playerCol - 3] = 0;
                    playerScore++;
                }
                wait(10, 32000);
                lcd_send_command(playerRow + playerCol - 1);
                lcd_send_data(' ');
                lcd_send_command(playerRow + playerCol - 2);
                lcd_send_data(' ');
                lcd_send_command(playerRow + playerCol - 3);
                lcd_send_data(' ');
            }
            else if (PLAYER == 1)
            {
                lcd_send_command(playerRow + playerCol + 1);
                lcd_send_data(PLAYER_ATTACK_RIGHT);
                if (DISPLAY_POSITIONS[playerRowNum][playerCol + 1] == ENEMY_COMING_RIGHT || DISPLAY_POSITIONS[playerRowNum][playerCol + 2] == ENEMY_COMING_RIGHT || DISPLAY_POSITIONS[playerRowNum][playerCol + 3] == ENEMY_COMING_RIGHT)
                {
                    DISPLAY_POSITIONS[playerRowNum][playerCol + 1] = 0;
                    DISPLAY_POSITIONS[playerRowNum][playerCol + 2] = 0;
                    DISPLAY_POSITIONS[playerRowNum][playerCol + 3] = 0;
                    playerScore++;
                }
                wait(10, 32000);
                lcd_send_command(playerRow + playerCol + 1);
                lcd_send_data(' ');
                lcd_send_command(playerRow + playerCol + 2);
                lcd_send_data(' ');
                lcd_send_command(playerRow + playerCol + 3);
                lcd_send_data(' ');
            }
        }

        // move enemies
        enemyMovement();

        // game over
        if (isPlayerDead())
        {
            customSleep(1);
            clearDisplay();
            lcd_send_line1("   GAME OVER!");
            char score[4];
            snprintf(score, sizeof(score), "%d", playerScore);
            char *scoreText = strcat(" YOUR SCORE: ", score);
            lcd_send_line2(scoreText);
            break;
        }

        // unlock buttons
        button_unlock();
    }

    return 0;
}