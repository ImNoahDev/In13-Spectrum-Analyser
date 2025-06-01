const int MSGEQ7_STROBE_PIN = 25;
const int MSGEQ7_RESET_PIN = 26;
const int MSGEQ7_OUT_PIN = 34;

const int NIXIE_CTRL_PINS[] = {13, 12, 14, 27, 33, 32, 15};
const int NUM_BANDS = 7;

const int VOLUME_POT_PIN = 35;
const int BRIGHTNESS_POT_PIN = 36;
const int ESP32_HV_CONTROL_PIN = 23;

const int PWM_FREQ = 5000;
const int PWM_RESOLUTION = 8;
int ledc_channels[NUM_BANDS];

int band_values[NUM_BANDS];

const unsigned long INACTIVITY_TIMEOUT_MS = 4 * 60 * 1000;
unsigned long last_activity_time = 0;
bool tubes_are_on = true;

const int ADC_MAX = 4095;
const int PWM_MAX = (1 << PWM_RESOLUTION) - 1;

const int ACTIVITY_NOISE_THRESHOLD = 50;

void setup() {
  Serial.begin(115200);
  Serial.println("Starting...");

  pinMode(MSGEQ7_STROBE_PIN, OUTPUT);
  pinMode(MSGEQ7_RESET_PIN, OUTPUT);
  pinMode(ESP32_HV_CONTROL_PIN, OUTPUT);

  digitalWrite(MSGEQ7_RESET_PIN, LOW);
  digitalWrite(MSGEQ7_STROBE_PIN, HIGH);
  digitalWrite(ESP32_HV_CONTROL_PIN, HIGH);

  for (int i = 0; i < NUM_BANDS; i++) {
    ledc_channels[i] = i;
    ledcSetup(ledc_channels[i], PWM_FREQ, PWM_RESOLUTION);
    ledcAttachPin(NIXIE_CTRL_PINS[i], ledc_channels[i]);
  }

  digitalWrite(MSGEQ7_RESET_PIN, HIGH);
  delayMicroseconds(5);
  digitalWrite(MSGEQ7_RESET_PIN, LOW);
  delayMicroseconds(75);

  last_activity_time = millis();
  tubes_are_on = true;

  Serial.println("Initialized.");
}

void readMSGEQ7() {
  digitalWrite(MSGEQ7_RESET_PIN, HIGH);
  delayMicroseconds(5);
  digitalWrite(MSGEQ7_RESET_PIN, LOW);
  delayMicroseconds(75);

  for (int i = 0; i < NUM_BANDS; i++) {
    digitalWrite(MSGEQ7_STROBE_PIN, LOW);
    delayMicroseconds(36);
    band_values[i] = analogRead(MSGEQ7_OUT_PIN);
    digitalWrite(MSGEQ7_STROBE_PIN, HIGH);
    delayMicroseconds(36);
  }
}

void turnOffTubes() {
  if (tubes_are_on) {
    Serial.println("Sleeping...");
    for (int i = 0; i < NUM_BANDS; i++) {
      ledcWrite(ledc_channels[i], 0);
    }
    digitalWrite(ESP32_HV_CONTROL_PIN, LOW);
  }
}

void updateTubeDisplay(float volume_level, float brightness_level) {
  for (int i = 0; i < NUM_BANDS; i++) {
    float current_band_value = band_values[i];
    float normalized_band = current_band_value / (float)ADC_MAX;
    float volume_adjusted_band = normalized_band * volume_level;
    volume_adjusted_band = constrain(volume_adjusted_band, 0.0, 1.0);
    int pwm_duty = (int)(volume_adjusted_band * brightness_level * PWM_MAX);
    pwm_duty = constrain(pwm_duty, 0, PWM_MAX);
    ledcWrite(ledc_channels[i], pwm_duty);
  }
}

void loop() {
  int raw_volume = analogRead(VOLUME_POT_PIN);
  int raw_brightness = analogRead(BRIGHTNESS_POT_PIN);

  float volume_setting = raw_volume / (float)ADC_MAX;
  float brightness_setting = raw_brightness / (float)ADC_MAX;

  readMSGEQ7();

  int max_band_value_this_cycle = 0;
  for (int i = 0; i < NUM_BANDS; i++) {
    if (band_values[i] > max_band_value_this_cycle) {
      max_band_value_this_cycle = band_values[i];
    }
  }

  if (max_band_value_this_cycle > ACTIVITY_NOISE_THRESHOLD) {
    if (!tubes_are_on) {
      Serial.println("Waking up...");
      digitalWrite(ESP32_HV_CONTROL_PIN, HIGH);
    }
    last_activity_time = millis();
    tubes_are_on = true;
  }

  if (millis() - last_activity_time > INACTIVITY_TIMEOUT_MS) {
    if (tubes_are_on) {
      turnOffTubes();
      tubes_are_on = false;
    }
  }

  if (tubes_are_on) {
    updateTubeDisplay(volume_setting, brightness_setting);
  }

  delay(20);
}