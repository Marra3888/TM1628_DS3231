#include <Wire.h>
#include <RTClib.h>
#include "TM1628.h"

// TM1628: DIO=D9, CLK=D8, STB=D7
TM1628 dvdLED(9, 8, 7, 5);
RTC_DS3231 rtc;

// Шрифт 0..F (bit1=f, bit2=a, bit3=e, bit4=b, bit5=d, bit6=c, bit7=g, bit0=DP)
const uint8_t PROGMEM NUMBER_FONT[] = {
  0b01111110, // 0
  0b01010000, // 1
  0b10111100, // 2
  0b11110100, // 3
  0b11010010, // 4
  0b11100110, // 5
  0b11101110, // 6
  0b01010100, // 7
  0b11111110, // 8
  0b11110110, // 9
  0b11011110, // A
  0b11101010, // b
  0b00101110, // C
  0b11111000, // d
  0b10101110, // E
  0b10001110  // F
};
inline uint8_t segPattern(uint8_t d) {
  return pgm_read_byte(&NUMBER_FONT[d & 0x0F]);
}

// Маски сегментов по вашей раскладке
const uint8_t SEG_A = (1 << 2);
const uint8_t SEG_B = (1 << 4);
const uint8_t SEG_C = (1 << 6);
const uint8_t SEG_D = (1 << 5);
const uint8_t SEG_E = (1 << 3);
const uint8_t SEG_F = (1 << 1);
const uint8_t SEG_G = (1 << 7);

// Индексы сегментов (без конфликтов с avr/io.h)
enum { IDX_A=0, IDX_B, IDX_C, IDX_D, IDX_E, IDX_F, IDX_G };
const uint8_t SEG_MASK_BY_IDX[7] = { SEG_A, SEG_B, SEG_C, SEG_D, SEG_E, SEG_F, SEG_G };

// Маршруты цифр (ваш порядок). 0xFF — конец.
const uint8_t PROGMEM DIGIT_ROUTE[10][7] = {
  { IDX_A, IDX_B, IDX_C, IDX_D, IDX_E, IDX_F, 0xFF },        // 0: a b c d e f
  { IDX_B, IDX_C, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF },            // 1: b c
  { IDX_A, IDX_B, IDX_G, IDX_E, IDX_D, 0xFF, 0xFF },         // 2: a b g e d
  { IDX_A, IDX_B, IDX_G, IDX_C, IDX_D, 0xFF, 0xFF },         // 3: a b g c d
  { IDX_F, IDX_G, IDX_B, IDX_C, 0xFF, 0xFF, 0xFF },          // 4: f g b c
  { IDX_A, IDX_F, IDX_G, IDX_C, IDX_D, 0xFF, 0xFF },         // 5: a f g c d
  { IDX_A, IDX_F, IDX_G, IDX_C, IDX_D, IDX_E, 0xFF },        // 6: a f g c d e
  { IDX_A, IDX_B, IDX_C, 0xFF, 0xFF, 0xFF, 0xFF },           // 7: a b c
  { IDX_A, IDX_B, IDX_C, IDX_D, IDX_E, IDX_F, IDX_G },       // 8: a b c d e f g
  { IDX_D, IDX_C, IDX_B, IDX_A, IDX_F, IDX_G, 0xFF }         // 9: d c b a f g
};

// Утилиты вывода
void setColon(bool on) { dvdLED.setSegments(on ? (1 << 3) : 0, 4); }
void blankAllDigits()  { for (uint8_t p=0; p<4; ++p) dvdLED.setSegments(0, p); }
void drawDigits(uint8_t hh, uint8_t mm) {
  dvdLED.setSegments(segPattern((hh / 10) % 10), 0);
  dvdLED.setSegments(segPattern(hh % 10),        1);
  dvdLED.setSegments(segPattern((mm / 10) % 10), 2);
  dvdLED.setSegments(segPattern(mm % 10),        3);
}

// Прокрутка ТОЛЬКО на одной позиции: 2 цикла a..f (без g), остальные позиции — без изменений
void sweepPosition(uint8_t pos, uint8_t cycles=2, uint16_t stepMs=120) {
  const uint8_t ORDER6[6] = { SEG_A, SEG_B, SEG_C, SEG_D, SEG_E, SEG_F };
  for (uint8_t k=0; k<cycles; ++k) {
    for (uint8_t i=0; i<6; ++i) {
      dvdLED.setSegments(ORDER6[i], pos);
      delay(stepMs);
    }
  }
}

// Анимация одной цифры по её маршруту (добавляем только нужные сегменты)
void animateDigitByRoute(uint8_t pos, uint8_t digit, uint16_t stepMs) {
  uint8_t finalPat = segPattern(digit);
  uint8_t acc = 0;
  for (uint8_t i=0; i<7; ++i) {
    uint8_t idx = pgm_read_byte(&DIGIT_ROUTE[digit][i]);
    if (idx == 0xFF) break;
    uint8_t mask = SEG_MASK_BY_IDX[idx];
    if (finalPat & mask) {
      acc |= mask;
      dvdLED.setSegments(acc, pos);
      delay(stepMs);
    }
  }
  dvdLED.setSegments(finalPat, pos); // итоговое состояние
}

// Построение времени: для каждой позиции по очереди
// 1) 2× прокрутка a..f на этой позиции
// 2) анимация цифры по её маршруту
void stagedTimePerPosition(uint8_t hh, uint8_t mm, uint16_t stepMs=110) {
  uint8_t dig[4] = {
    (uint8_t)((hh / 10) % 10),
    (uint8_t)(hh % 10),
    (uint8_t)((mm / 10) % 10),
    (uint8_t)(mm % 10)
  };

  blankAllDigits();     // на старте гасим всё
  for (uint8_t pos=0; pos<4; ++pos) {
    sweepPosition(pos, 2, stepMs);              // только текущая позиция
    animateDigitByRoute(pos, dig[pos], stepMs); // затем её цифра
  }
}

// ==== настройка по времени компиляции (выкл) ====
#define ADJUST_AT_BOOT 0

void setup() {
  Wire.begin();

  setColon(false);
  blankAllDigits();

  if (!rtc.begin()) { drawDigits(88,88); setColon(true); while(1){} }
#if ADJUST_AT_BOOT
  rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));
#endif

  // 1) Читаем текущее время
  DateTime t = rtc.now();

  // 2) Для позиции 0 → 1 → 2 → 3:
  //    прокрутка (2× a..f) и затем «сборка» цифры по маршруту
  stagedTimePerPosition(t.hour(), t.minute(), 110);
}

void loop() {
  static uint32_t t250 = 0;   // тик 250 мс (4 Гц)
  static bool colon = false;
  static uint8_t prevH = 255, prevM = 255;

  uint32_t ms = millis();
  if ((uint32_t)(ms - t250) >= 250) {
    t250 += 250;

    // «:» — 2 раза/сек
    colon = !colon;
    setColon(colon);

    // Опрос RTC 4 раза/сек; обновляем цифры только при смене hh/mm
    DateTime now = rtc.now();
    uint8_t hh = now.hour();
    uint8_t mm = now.minute();
    if (hh != prevH || mm != prevM) {
      prevH = hh; prevM = mm;
      drawDigits(hh, mm);   // мгновенное обновление
    }
  }
}