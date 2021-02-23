/****************************************************************************************
   7セグ表示処理

   @yankee様のコードを流用させて頂きました。
      https://qiita.com/yankee/items/1591724d8a2722951c7e

****************************************************************************************/


// 7セグ表示関連
#define LCD_LARGE_BAR_WIDTH (10)
#define LCD_LARGE_BAR_LENGTH (30)
#define LCD_LARGE_BAR_CORNER_RADIUS (6)

#define LCD_SMALL_BAR_WIDTH (4)
#define LCD_SMALL_BAR_LENGTH (12)
#define LCD_SMALL_BAR_CORNER_RADIUS (3)

void DrawNuber7seg_S(uint8_t number, uint8_t x_start, uint8_t y_start, uint16_t forecolor,uint16_t backcolor);
void DrawNuber7seg_L(uint8_t number, uint8_t x_start, uint8_t y_start, uint16_t forecolor,uint16_t backcolor);

