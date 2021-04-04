
/*
 * This Arduino sketch implements a fuel gauge for a 1966 Rambler. This car has a sensor in the fuel tank 
 * whose resistance varies based on how full the tank is. This sketch uses a voltage divider to measure this 
 * resistance, and from that calculate how close to full/empty the tank is.
 * 
 * This runs on an Arduino Uno with a connected 16x2 LCD text display. The circuit is laid out like this:
 * 
 *                                           LCD
 *                                        +--------+
 *                                  +-----|VSS     |
 *                     ARDUINO      | +---|VEE     |
 *                  +-------------+ | | +-|RW      |
 *    Accessory-----|Power+    Gnd|-+-+-+-|LED-    |
 *      Chassis-----|Power-     D2|-------|14      |
 *  Fuel Sender--+--|A0         D3|-------|13      |
 *               |  |           D4|-------|12      |
 *               |  |           D5|-------|11      |
 *               |  |          D11|-------|6       |
 *               |  |          D12|-------|4       |
 *               |  |           5V|-+--+--|VCC LED+|--+
 *               |  +-------------+ |  |  +--------+  |
 *               |                  |  |              |
 *               +---(100 ohm)------+  +----(200 ohm)-+
 */


#include <LiquidCrystal.h>
LiquidCrystal lcd(12,11,5,4,3,2);


float estimate_ohmage_from_reading(int reading) {
  /*
   * Initial test measurments used a potentiometer to simulate the gas sensor
   * This was put in series with a 100 ohm resistor to make a voltage divider.
   * The voltage between the two was then sampled by an analog pin on the Arduino
   * for a few data points.
   * 
   * resistance | value read
   *     2      | 80
   *    26      | 243
   *    45      | 339
   *    75      | 443
   *   102      | 534
   *    
   * To get a formula for this, I "normalized" each resistance and value read so that they ranged between 0
   * and 1. Then, I looked for a curve fit. Theoretically, this should be a hyperbola, but I was able to find
   * a parabola that fits acceptably well, and is much easier to deal with. The fit ended up being:
   *    y = 0.5387*x2 + 0.4665*x
   */
  float norm_reading = (reading - 80) / 454.0;
  float norm_ohmage = 0.5387*norm_reading*norm_reading + 0.4665*norm_reading;
  return 100.0*norm_ohmage + 2.0;
}

float get_gallons_for_ohmage(float ohmage) {
  /*
   * According to the specs for the car, the fuel sender will register 73 ohms with an empty tank,
   * and 10 ohms with a full tank. This has not yet been verified with real measurements, but I did measure
   * 22 ohms at the tank, 24 miles after a fill-up.
   * 
   * The specs also say that the tank holds 19 gallons.
   * 
   * The below formulas can be tweaked if/when real-world measurements dictate, but for now we'll assume everything
   * is working to spec, and that the resistance response is linear with gallons in tank.
   * 
   * That gives a line slope of -0.3016 and an intercept of 22.016
   */

   return -0.3016*ohmage + 22.016;  
}

int get_num_display_blocks_for_gallons(float gallons) {
  /*
   * This is a 19 gallon tank.
   * 
   * We have 17 possible choices for display state (we can show anywhere from 0 to 16 blocks)
   * 
   * So, we can just say that each block corresponds to a gallon. If the display has all 16 blocks, then the tank 
   * has 18-19 gallons of gas; with 15 blocks, it's 17-18 gallons.... ; with 1 block, it's 3-4 gallons. With no
   * blocks, it's less than 3 gallons. This gives us some safety margin both in terms of not running out of gas, and 
   * also in terms of not slurping up whatever sediments might be at the bottom of that gas tank.
   */
   int gallon_floor = (int)gallons;
   int desired_blocks = gallon_floor - 2;
   return max(0, min(desired_blocks, 16));
}

// Special-purpose characters
const uint8_t INVERSE_G = 0;
const uint8_t INVERSE_A = 1;
const uint8_t INVERSE_S = 2;
const uint8_t FILLED_BLOCK = 255; // Already defined in display ROM
const uint8_t EMPTY_BLOCK = 32;   // ASCII space character
const uint8_t OHMS = 244;         // Greek omega character


void show_gas_meter(int num_blocks) {
  lcd.setCursor(0,0);

  if (num_blocks < 8) {
    /*
     * Show a plain gas meter on the left side, then write the word "GAS" in normal text on the right
     */
    for (uint8_t col = 0; col <  num_blocks; ++col) {
      lcd.write(FILLED_BLOCK);
    }
    for (uint8_t col = num_blocks; col < 13; ++col) {
      lcd.write(EMPTY_BLOCK);
    }
    lcd.write('G');
    lcd.write('A');
    lcd.write('S');
  } else {
    /*
     * The gas meter will be on the left side, with the word "GAS" in inverse text.
     */
    lcd.write(INVERSE_G);
    lcd.write(INVERSE_A);
    lcd.write(INVERSE_S);
    for (uint8_t col = 3; col < num_blocks; ++col) {
      lcd.write(FILLED_BLOCK);
    }
    for (uint8_t col = num_blocks; col < 16; ++col) {
      lcd.write(EMPTY_BLOCK);
    }
  }  
}


void print_number(int number, int digits, int radix) {
  int residue = number;
  for (int i = digits-1; i >= 0; --i) {
    int placeval = 1;
    for (int power = 0; power < i; ++power) {
      placeval *= 10;
    }
    int this_digit = residue / placeval;
    lcd.write('0'+this_digit);
    residue = residue - (this_digit * placeval);
    if (radix == i) {
      lcd.write('.');
    }
  }
}


void show_gas_stats(int sensor_value, float ohmage, float gallons) {
    lcd.setCursor(0,1);
    print_number(sensor_value, 4, -1);
    lcd.write(EMPTY_BLOCK);
    print_number(ohmage, 4, -1);
    lcd.write(OHMS);
    lcd.write(EMPTY_BLOCK);
    print_number(gallons*10, 3, 1);
    lcd.write('g');
}


const unsigned int boot_time_ms = millis();
unsigned int last_update_time_ms = boot_time_ms;
bool in_startup_mode = true;
const int STARTUP_SAMPLE_RATE_MS = 500;
const int STARTUP_SAMPLE_DURATION_S = 5;
const int NORMAL_SAMPLE_RATE_S = 5;

void loop() {

  // Compute time since our last update, treating overflow as a long duration
  unsigned int current_time_ms = millis();
  unsigned int delta_ms = (current_time_ms < last_update_time_ms) ? 30000 : current_time_ms - last_update_time_ms;
  Serial.print("Startup?: "); Serial.println(in_startup_mode, DEC);
  Serial.print("Previous: "); Serial.println(last_update_time_ms, DEC);
  Serial.print("Current: "); Serial.println(current_time_ms, DEC);
  Serial.print("Delta: "); Serial.println(delta_ms, DEC);
  Serial.println();

  // Early return if it's not time to sample yet
  if (  (delta_ms < STARTUP_SAMPLE_RATE_MS) || ( (!in_startup_mode) && (delta_ms < NORMAL_SAMPLE_RATE_S*1000)) ) return;

  Serial.println("Passed early return check");
  // Switch to "normal mode" after the startup time is complete.
  if (in_startup_mode) {
    unsigned int time_since_boot_ms = current_time_ms - boot_time_ms;
    in_startup_mode = (time_since_boot_ms < STARTUP_SAMPLE_DURATION_S * 1000);
  }

  // Sample the voltage divider
  int sensor_value = analogRead(A0);

  // Update the gauge
  float ohmage = estimate_ohmage_from_reading(sensor_value);
  float gallons = get_gallons_for_ohmage(ohmage);
  int num_blocks = get_num_display_blocks_for_gallons(gallons);
  show_gas_meter(num_blocks);
  show_gas_stats(sensor_value, ohmage, gallons);

  last_update_time_ms = current_time_ms;
}

/*
 * Custom-defined inverse-text characters to spell "GAS" on the meter
 */
uint8_t inverse_g_def[8] = {
  B10001, 
  B01110, 
  B01111,
  B01000,
  B01110,
  B01110,
  B10000,
  B11111,
};

uint8_t inverse_a_def[8] = {
  B10001, 
  B01110, 
  B01110, 
  B01110, 
  B00000,
  B01110,
  B01110,
  B11111,
};

uint8_t inverse_s_def[8] = {
  B10000, 
  B01111,
  B01111,
  B10001,
  B11110,
  B11110,
  B00001,
  B11111  
};

void setup() {
  lcd.createChar(INVERSE_G, inverse_g_def);
  lcd.createChar(INVERSE_A, inverse_a_def);
  lcd.createChar(INVERSE_S, inverse_s_def);
  lcd.begin(16,2);
  Serial.begin(9600);
}
