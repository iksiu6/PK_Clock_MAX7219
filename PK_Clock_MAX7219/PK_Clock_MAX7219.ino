//Zegar z synchronizacją z serwerem NTP, czujnikiem temperatury, wilgotności i ciśnienia
//Ustawianie połączenia z WiFi za pomocą managera połączeń - bez konieczności modyfikacji kodu o SSID i PSWD
//Testowane w środowisku Arduino IDE 1.8.13(Windows Store 1.8.42.0)
//Pierwsze uruchomienie - wyszukać WiFi Set WiFi Connection - połączyć się i ustanowić połączenie

//Użyta plytka NodeMCU 1.0 (ESP_12E Module)

//Opis połączeń:

// LED Matrix Pin -> ESP8266 Pin
// Vcc            -> 3,3v 
// Gnd            -> Gnd 
// DIN            -> D7  
// CS             -> D4  
// CLK            -> D5  

//BME280 pins
//Vcc             -> 3,3v 
//Gnd             -> Gnd 
//SDA             -> D2
//SCL             -> D1



#include <PubSubClient.h>
#include <Adafruit_BME280.h>
#include <Adafruit_GFX.h>
#include <Max72xxPanel.h>
#include <time.h>
#include <WiFiManager.h> //https://github.com/tzapu/WiFiManager WiFi Configuration Magic

byte ASCIIvalue = 42;
int pinCS = D4;
int numberOfHorizontalDisplays = 4;
int numberOfVerticalDisplays   = 1;
char time_value[20];
int wait = 70; // In milliseconds
int spacer = 1;
int width  = 5 + spacer; // The font width is 5 pixels
int m = 0;
String t, h, p;
float temp_corr = (-1.5);
float hum_corr = 10 ;
unsigned long lastMsg = 0;
int licznik = 0;
int interval = 6; //1 = 10sekund 6 = minuta

Max72xxPanel matrix = Max72xxPanel(pinCS, numberOfHorizontalDisplays, numberOfVerticalDisplays);

WiFiClient espClient;
PubSubClient client(espClient);


//Konfiguracja pod Homeassistant

//IP Homeassistent-a
byte mqtt_server[] = {192, 168, 3, 122};

#define zegar1temp "sensor/temperature1"
#define zegar1hum "sensor/humidity1"
#define zegar1press "sensor/presure1"

WiFiServer server(80);

Adafruit_BME280 bme; // use I2C interface
Adafruit_Sensor *bme_temp = bme.getTemperatureSensor();
Adafruit_Sensor *bme_pressure = bme.getPressureSensor();
Adafruit_Sensor *bme_humidity = bme.getHumiditySensor();


/*****************************************************************
  /* convert float to string with a                                *
  /* precision of two decimal places                               *
  /*****************************************************************/
String Float2String(const float value)
{
  // Convert a float to String with two decimals.
  char temp[15];
  String s;

  dtostrf(value, 13, 2, temp);
  s = String(temp);
  s.trim();
  return s;
}


/*****************************************************************
  /* Debug output                                                  *
  /*****************************************************************/
void debug_out(const String &text, int linebreak = 1)
{
  if (linebreak)
  {
    Serial.println(text);
  }
  else
  {
    Serial.print(text);
  }
}


/*****************************************************************
  /* Setup                                                         *
  /*****************************************************************/
void setup() {
  Serial.begin(9600);

  Serial.println(F("BME280 Sensor event test"));

  if (!bme.begin()) {
    delay(2000);
    Serial.println(F("Could not find a valid BME280 sensor, check wiring!"));
    while (1) delay(10);
  }

  client.setServer(mqtt_server, 1883);
  Wire.begin();
  delay(10);

  setenv("TZ", "GMT-1BST", 1);

  matrix.setIntensity(m); // Use a value between 0 and 15 for brightness
  matrix.setRotation(0, 1);    // The first display is position upside down
  matrix.setRotation(1, 1);    // The first display is position upside down
  matrix.setRotation(2, 1);    // The first display is position upside down
  matrix.setRotation(3, 1);    // The first display is position upside down
  matrix.fillScreen(LOW);
  matrix.write();

  if (WiFi.status() != WL_CONNECTED) {
    matrix.drawChar(2, 0, 'W', HIGH, LOW, 1);
    matrix.drawChar(8, 0, 'I', HIGH, LOW, 1);
    matrix.drawChar(13, 0, '-', HIGH, LOW, 1);
    matrix.drawChar(20, 0, 'F', HIGH, LOW, 1);
    matrix.drawChar(26, 0, 'I', HIGH, LOW, 1);
    matrix.write(); // Send bitmap to display
  }



  WiFiManager wifimanager;
  wifimanager.setConfigPortalTimeout(30);

  if (!wifimanager.autoConnect("Set_WiFi_Connection")) {
    Serial.println("failed to connect and hit timeout - ESP RESET");
    //reset and try again, or maybe put it to deep sleep
    matrix.drawChar(2, 0, 'R', HIGH, LOW, 1);
    matrix.drawChar(8, 0, 'E', HIGH, LOW, 1);
    matrix.drawChar(14, 0, 'S', HIGH, LOW, 1);
    matrix.drawChar(20, 0, 'E', HIGH, LOW, 1);
    matrix.drawChar(26, 0, 'T', HIGH, LOW, 1);
    matrix.write(); // Send bitmap to display
    delay(2000);
    ESP.restart();
    delay(1000);
  }

  // Connect to WiFi network
  Serial.println();
  Serial.println();
  Serial.println("WiFi connected");
  Serial.print("This device adress: ");
  Serial.print(WiFi.localIP());



  // Ustawienia serwera NTP
  
  configTime(0 * 3600, 0, "192.168.3.1");
  //configTime(0 * 3600, 0, "it.pool.ntp.org", "time.nist.gov");
  Serial.println("\nWaiting for time");

  delay(1000);

}

bool checkBound(float newValue, float prevValue, float maxDiff) {
  return !isnan(newValue) &&
         (newValue < prevValue - maxDiff || newValue > prevValue + maxDiff);

}

void reconnect() {
  if (!client.connected()) {
    Serial.print("Attempting MQTT connection...");
    if (client.connect("PK_Arduino_Temp")) {
      Serial.println("connected");
    } else {
      Serial.print("failed, rc=");
      Serial.print(client.state());
      Serial.println(" try again in 5 seconds");
      matrix.fillScreen(LOW);
      wait = 40;
      display_message("MQTT Error");
    }
  }
}



void loop() {

  sensors_event_t temp_event, pressure_event, humidity_event;
  bme_temp->getEvent(&temp_event);
  bme_pressure->getEvent(&pressure_event);
  bme_humidity->getEvent(&humidity_event);

  time_t now = time(nullptr);


  //Ustawienie jasności wyświetlacza w zależności od godziny
  
  if (time_value[0] == 0  && time_value[1] == 0) {
    m = 0;
  }
  if (time_value[0] == 0  && time_value[1] == 1) {
    m = 0;
  }
  if (time_value[0] == 0  && time_value[1] == 2) {
    m = 0;
  }
  if (time_value[0] == 0  && time_value[1] == 3) {
    m = 0;
  }
  if (time_value[0] == 0  && time_value[1] == 4) {
    m = 0;
  }
  if (time_value[0] == 0  && time_value[1] == 5) {
    m = 0;
  }
  if (time_value[0] == 0  && time_value[1] == 6) {
    m = 0;
  }
  if (time_value[0] == 0  && time_value[1] == 7) {
    m = 4;
  }
  if (time_value[0] == 0  && time_value[1] == 8) {
    m = 7;
  }
  if (time_value[0] == 0  && time_value[1] == 9) {
    m = 11;
  }


  if (time_value[0] == 1  && time_value[1] == 0) {
    m = 15;
  }
  if (time_value[0] == 1  && time_value[1] == 1) {
    m = 15;
  }
  if (time_value[0] == 1  && time_value[1] == 2) {
    m = 15;
  }
  if (time_value[0] == 1  && time_value[1] == 3) {
    m = 15;
  }
  if (time_value[0] == 1  && time_value[1] == 4) {
    m = 15;
  }
  if (time_value[0] == 1  && time_value[1] == 5) {
    m = 15;
  }
  if (time_value[0] == 1  && time_value[1] == 6) {
    m = 15;
  }
  if (time_value[0] == 1  && time_value[1] == 7) {
    m = 12;
  }
  if (time_value[0] == 1  && time_value[1] == 8) {
    m = 10;
  }
  if (time_value[0] == 1  && time_value[1] == 9) {
    m = 8;
  }


  if (time_value[0] == 2  && time_value[1] == 0) {
    m = 5;
  }
  if (time_value[0] == 2  && time_value[1] == 1) {
    m = 2;
  }
  if (time_value[0] == 2  && time_value[1] == 2) {
    m = 1;
  }
  if (time_value[0] == 2  && time_value[1] == 3) {
    m = 0;
  }
Serial.println("brightness = ");
  Serial.print(m);
  matrix.setIntensity(m);
  matrix.fillScreen(LOW);

  if (time(nullptr) <= 100000) {
    matrix.fillScreen(LOW);
    wait = 40;
    display_message("Brak internetu - godzina nieaktualna");

  }
  Serial.println(ctime(&now));
  String time = String(ctime(&now));
  time.trim();
  Serial.println(time);
  time.substring(11, 19).toCharArray(time_value, 10);
  matrix.drawChar(2, 0, time_value[0], HIGH, LOW, 1); // H
  matrix.drawChar(8, 0, time_value[1], HIGH, LOW, 1); // HH
  matrix.drawChar(14, 0, time_value[2], HIGH, LOW, 1); // HH:
  matrix.drawChar(20, 0, time_value[3], HIGH, LOW, 1); // HH:M
  matrix.drawChar(26, 0, time_value[4], HIGH, LOW, 1); // HH:MM
  matrix.write(); // Send bitmap to display


  debug_out("temp     : " + Float2String(float(temp_event.temperature + temp_corr)), 1);
  debug_out("humidity : " + Float2String(float(humidity_event.relative_humidity + hum_corr)), 1);
  debug_out("pressure : " + Float2String(float(pressure_event.pressure)), 1);


  delay(10000);

  matrix.fillScreen(LOW);
  h = (String)(int)(humidity_event.relative_humidity + hum_corr);
  //t = (String)(int)(temp_event.temperature + temp_corr);
  t = String((temp_event.temperature + temp_corr)).c_str(), true;
  p = (String)(int)pressure_event.pressure;
  wait = 60;
  display_message(t + (char)247 + "C " + h + "% " + p + "hPa");


  if (!client.connected()) {
    reconnect();
  }
  client.loop();

  if (licznik >= interval) {
    unsigned long now = millis();
    if (now - lastMsg > 1000) {
      lastMsg = now;
      client.publish(zegar1temp , String((temp_event.temperature + temp_corr)).c_str(), true);
      client.publish(zegar1hum , String((humidity_event.relative_humidity + hum_corr)).c_str(), true);
      client.publish(zegar1press , String((pressure_event.pressure)).c_str(), true);
      Serial.println("Dane przesłane poprzez MQTT");
    }
    licznik = 0;
  }
  else
    licznik = licznik + 1;
      Serial.println(licznik);
}
void display_message(String message) {
  for ( int i = 0 ; i < width * message.length() + matrix.width() - spacer; i++ ) {
    //matrix.fillScreen(LOW);
    int letter = i / width;
    int x = (matrix.width() - 1) - i % width;
    int y = (matrix.height() - 8) / 2; // center the text vertically
    while ( x + width - spacer >= 0 && letter >= 0 ) {
      if ( letter < message.length() ) {
        matrix.drawChar(x, y, message[letter], HIGH, LOW, 1); // HIGH LOW means foreground ON, background off, reverse to invert the image
      }
      letter--;
      x -= width;
    }
    matrix.write(); // Send bitmap to display
    delay(wait / 2);
  }
}

void sendReset() {
  Serial.println("Sending Reset command");
  Serial.println(1 / 0);
}
