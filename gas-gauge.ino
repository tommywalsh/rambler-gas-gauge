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
 *  Fuel Sender--+--|A1         D3|-------|13      |
 *               |  |           D4|-------|12      |
 *               |  |           D5|-------|11      |
 *               |  |          D11|-------|6       |
 *               |  |          D12|-------|4       |
 *               |  |           5V|---+---|VCC LED+|--+
 *               |  +-------------+   |   +--------+  |
 *               |                    |               |
 *               +---(100 ohm)--------+----(200 ohm)--+
 */


#include <LiquidCrystal.h>
LiquidCrystal lcd(12,11,5,4,3,2);


float estimate_gallons_remaining_from_reading(float reading) {
  /*
   * Initial testing in the car gives a linear fit formula like this:
   * (gallons short of full) = (0.0637) * (reading) - 7.3
   * 
   * The car specs show this to be a 19 gallon tank, so the gallons remaining should be:
   * (gallons remaining) = 26.3 - (0.0637 * reading)
   */
   return 26.3 - (0.0637 * reading);
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


void show_gas_stats(int row, int sensor_value, float gallons) {
    lcd.setCursor(0,row);
    print_number(sensor_value, 4, -1);
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
const int MAX_SAMPLES = 100;
int samples[MAX_SAMPLES];
int next_sample = 0;
int samples_to_average = 0;

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
  int sensor_value = analogRead(A1);

  // Update the cache of samples
  samples[next_sample] = sensor_value;
  ++next_sample;
  samples_to_average = min(samples_to_average + 1, MAX_SAMPLES);
  if (next_sample >= MAX_SAMPLES) {
    next_sample = 0;
  }

  // Calculate the average sensor value in the cache
  float sum = 0.0;
  for (int i = 0; i < samples_to_average; ++i) {
    sum += samples[i];
  }
  float average_sensor_value = sum / samples_to_average;

  // Update the gauge
  float gallons = estimate_gallons_remaining_from_reading(average_sensor_value);
  int num_blocks = get_num_display_blocks_for_gallons(gallons);
  show_gas_meter(num_blocks);
  show_gas_stats(1, average_sensor_value, gallons);

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
