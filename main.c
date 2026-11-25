#ifndef F_CPU
#define F_CPU 16000000UL
#endif

#include <avr/io.h>
#include <avr/interrupt.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>


/* ===================== Konfiguration ===================== */
#define UART0_BAUD      9600UL
#define VREF_MV         5000UL      /* Tilpas til f.eks. 3300UL ved 3.3V AVcc */
#define ADC_CHANNEL     0u          /* ADC0 = PC0 */
#define LEDPortValue    (1 << 1)
#define MaxNumbersInOutputString 64


volatile bool flag_read_adc = false;

/* ===================== Minimum UART0 (9600 baud, 8N1) ===================== */
static void uart0_init_9600(void) {
    uint16_t ubrr = (uint16_t)((F_CPU / (16UL * UART0_BAUD)) - 1UL);
    UBRR0H = (uint8_t)(ubrr >> 8);
    UBRR0L = (uint8_t)(ubrr & 0xFFu);

    UCSR0C = (1 << UCSZ01) | (1 << UCSZ00);
    UCSR0B = (1 << TXEN0);
}

static void uart0_putc(char c) {
    while (!(UCSR0A & (1 << UDRE0))) {
        /* Vent på ledig TX-buffer */
    }
    UDR0 = (uint8_t)c;
}

static void uart0_puts(const char *s) {
    while (*s != '\0') {
        uart0_putc(*s++);
    }
}

/* ===================== Timer1: 5 sekunders interrupt ===================== */
/*
 * Ved F_CPU = 16 MHz og prescaler = 1024:
 * Timer-frekvens = 16.000.000 / 1024 = 15.625 Hz (64 us pr. tick).
 * 5 sekunder = 5 * 15.625 = 78.125 ticks.
 * Da OCR1A er 16-bit (maks. 65.535), deles 5 sek. op i f.eks. 5 x 1 sekund
 * (15.625 ticks pr. sekund -> OCR1A = 15624).
 */
static void timer1_init_5s_interrupt(void) {
    TCCR1A = 0;
    TCCR1B = (1 << WGM12);          /* CTC mode: TOP = OCR1A */
    OCR1A  = 15624u;                /* 15.625 ticks = 1,000 s ved 16 MHz / 1024 */
    TIMSK1 = (1 << OCIE1A);         /* Aktiver Compare Match A interrupt */
    TCCR1B |= (1 << CS12) | (1 << CS10); /* Prescaler 1024 -> start timer */
}

ISR(TIMER1_COMPA_vect) {
    static uint8_t sec_count = 0;
    sec_count++;
    if (sec_count >= 5u) {
        PORTB ^= LEDPortValue;
        sec_count = 0;
        flag_read_adc = true;
    }
}

/* ===================== ADC (10-bit på valgt kanal) ===================== */
static void adc_init(uint8_t channel) {
    /* AVcc som reference (REFS0 = 1), vælg kanal 0..7 i MUX3:0 */
    ADMUX = (1 << REFS0) | (channel & 0x07u);

    /* Deaktiver digital input buffer på valgt ADC-pin for at spare strøm */
    if (channel < 6u) {
        DIDR0 |= (1 << channel);
    }

    /* Aktiver ADC og sæt prescaler til 128 (16 MHz / 128 = 125 kHz ADC-clock) */
    ADCSRA = (1 << ADEN) | (1 << ADPS2) | (1 << ADPS1) | (1 << ADPS0);
}

static uint16_t adc_read_10bit(void) {
    /* Start enkelt konvertering */
    ADCSRA |= (1 << ADSC);

    /* Vent indtil konverteringen er færdig */
    while (ADCSRA & (1 << ADSC)) {
        /* Polling på enkel ADC-konvertering */
    }

    return ADC; /* Læser ADCL først og derefter ADCH automatisk i avr-gcc */
}

/* ===================== Hovedprogram ===================== */
int main(void) {
    //char line[64];
    char *linePointer;

    DDRB |= LEDPortValue;
    uart0_init_9600();
    adc_init(ADC_CHANNEL);
    timer1_init_5s_interrupt();

    sei();
    uart0_puts("Klar: ADC0 maales hvert 5. sekund\r\n");

    while (1) {
        if (flag_read_adc) 
        {
            flag_read_adc = false;

            uint16_t raw = adc_read_10bit();
            uint32_t mv = ((uint32_t)raw * VREF_MV) / 1023UL;
            uint16_t volts_whole = (uint16_t)(mv / 1000UL);
            uint16_t volts_milli = (uint16_t)(mv % 1000UL);

            linePointer = malloc(MaxNumbersInOutputString);

            snprintf(
                linePointer,
                MaxNumbersInOutputString,
                "ADC: %u | Spaending: %u.%03u V (%lu mV)\r\n",
                raw,
                volts_whole,
                volts_milli,
                (unsigned long)mv
            );
            uart0_puts(linePointer);
            free(linePointer);
        }
    }
}