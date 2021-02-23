/****************************************************************************************
   7セグ表示処理

   @yankee様のコードを流用させて頂きました。
      https://qiita.com/yankee/items/1591724d8a2722951c7e

****************************************************************************************/
#include <M5Stack.h>
#include "7seg.h"

const uint8_t digits_normal[] =
{
    0b00111111, // 0
    0b00110000, // 1
    0b01101101, // 2
    0b01111001, // 3
    0b01110010, // 4
    0b01011011, // 5
    0b01011111, // 6
    0b00110011, // 7
    0b01111111, // 8
    0b01111011, // 9
    0b00000000, // off
};


void drawNumber(uint8_t number, uint8_t x_start, uint8_t y_start, uint8_t bar_width, uint8_t bar_length, uint8_t corner_radius, uint16_t forecolor,uint16_t backcolor)
{
  uint8_t bar_gap = bar_width >> 1;

  if (number > 10)
  {
    number = 10;
  }
  // top
  if (digits_normal[number] & 0b0000000000000001)
    M5.Lcd.fillRoundRect(x_start, y_start, bar_length, bar_width, corner_radius, forecolor);
  else
    M5.Lcd.fillRoundRect(x_start, y_start, bar_length, bar_width, corner_radius, backcolor);

  // upper-left
  if (digits_normal[number] & 0b0000000000000010)
    M5.Lcd.fillRoundRect((x_start - bar_gap * 2), (y_start + bar_gap), bar_width, bar_length, corner_radius, forecolor);
  else
    M5.Lcd.fillRoundRect((x_start - bar_gap * 2), (y_start + bar_gap), bar_width, bar_length, corner_radius, backcolor);

  // under-left
  if (digits_normal[number] & 0b0000000000000100)
    M5.Lcd.fillRoundRect((x_start - bar_gap * 2), (y_start + bar_gap + bar_length * 1), bar_width, bar_length, corner_radius, forecolor);
  else
    M5.Lcd.fillRoundRect((x_start - bar_gap * 2), (y_start + bar_gap + bar_length * 1), bar_width, bar_length, corner_radius, backcolor);

  // bottom
  if (digits_normal[number] & 0b0000000000001000)
    M5.Lcd.fillRoundRect(x_start, (y_start + bar_length * 2), bar_length, bar_width, corner_radius, forecolor);
  else
    M5.Lcd.fillRoundRect(x_start, (y_start + bar_length * 2), bar_length, bar_width, corner_radius, backcolor);

  // under-right
  if (digits_normal[number] & 0b0000000000010000)
    M5.Lcd.fillRoundRect((x_start + bar_length), (y_start + bar_gap + bar_length * 1), bar_width, bar_length, corner_radius, forecolor);
  else
    M5.Lcd.fillRoundRect((x_start + bar_length), (y_start + bar_gap + bar_length * 1), bar_width, bar_length, corner_radius, backcolor);

  // upper-right
  if (digits_normal[number] & 0b0000000000100000)
    M5.Lcd.fillRoundRect((x_start + bar_length), (y_start + bar_gap), bar_width, bar_length, corner_radius, forecolor);
  else
    M5.Lcd.fillRoundRect((x_start + bar_length), (y_start + bar_gap), bar_width, bar_length, corner_radius, backcolor);

  // center
  if (digits_normal[number] & 0b0000000001000000)
    M5.Lcd.fillRoundRect(x_start, (y_start + bar_length * 1), bar_length, bar_width, corner_radius, forecolor);
  else
    M5.Lcd.fillRoundRect(x_start, (y_start + bar_length * 1), bar_length, bar_width, corner_radius, backcolor);
}


void DrawNuber7seg_S(uint8_t number, uint8_t x_start, uint8_t y_start, uint16_t forecolor,uint16_t backcolor){
    drawNumber(number, x_start, y_start, LCD_SMALL_BAR_WIDTH, LCD_SMALL_BAR_LENGTH, LCD_SMALL_BAR_CORNER_RADIUS, forecolor, backcolor);
}

void DrawNuber7seg_L(uint8_t number, uint8_t x_start, uint8_t y_start, uint16_t forecolor,uint16_t backcolor){
    drawNumber(number, x_start, y_start, LCD_LARGE_BAR_WIDTH, LCD_LARGE_BAR_LENGTH, LCD_LARGE_BAR_CORNER_RADIUS, forecolor, backcolor);
}
