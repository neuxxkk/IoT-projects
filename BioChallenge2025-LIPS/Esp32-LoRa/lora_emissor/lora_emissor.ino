#include <WiFi.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <LittleFS.h>
#include <LoRa.h>
#include <SPI.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include "esp_sleep.h"
#include <time.h>

// Pinos do LoRa
#define SCK_LORA        5
#define MISO_LORA       19
#define MOSI_LORA       27
#define RESET_PIN_LORA  14
#define SS_PIN_LORA     18

// Parâmetros LoRa
#define HIGH_GAIN_LORA  14    // 0 ~ 20dBm
#define BAND            915E6 // 915MHz

// Pino do sensor de umidade do solo
#define SENSOR_PIN      36

// Definições do OLED
#define OLED_SDA_PIN    4
#define OLED_SCL_PIN    15
#define SCREEN_WIDTH    128
#define SCREEN_HEIGHT   64
#define OLED_ADDR       0x3C
#define OLED_RESET      16

// linhas do OLED
#define OLED_LINE1      0
#define OLED_LINE2      40
#define OLED_LINE3      50

// Identificadores
#define DEBUG_SERIAL_BAUDRATE 115200
#define TIMEOUT_MS 2000 // Tempo de espera Handshake
#define TIMEOUT_GLOBAL_MS 5000 // Espaçamento entre buscas por central

#define uS_TO_S_FACTOR 1000000ULL  // Conversion factor for micro seconds to seconds
#define TIME_TO_SLEEP  5           // Time ESP32 will go to sleep (in seconds)


// PRECISA CALIBRAR!
#define VALOR_SECO  2400
#define VALOR_UMIDO 1700

//Struct para dados do LoRa
struct LoraData {
  float umidade;
} data;

// Variáveis globais
bool central_exists;
bool central_exists_aux;
unsigned long last_central_check;

// WiFi
const char* ssid     = "Lips";
const char* password = "sala3086";

// Parâmetros de rede para ter IP estático
#define ACESS_POINT 0
IPAddress local_IP(192,168,ACESS_POINT,65);    // 172 é o IP do ponto de acesso
IPAddress gateway(192,168,ACESS_POINT,1);
IPAddress subnet(255,255,255,0);   

// Server declaration
AsyncWebServer server(80);

// Display setting
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

// Get time now
const char* ntpServer  = "pool.ntp.org";
const long  gmtOffset  = -3 * 3600;    
const int   daylightOffset = 0;   
long sleep_time_sec;

// all-time functions
bool display_init();
bool lora_chip_init();
float sensor_read();
void central_check();
void set_sleep_time();
void going_to_sleep();

// as-central functions
void server_init();
void wifi_connect();
void display_write_wifi();
void display_write_central();

// as-component functions
void display_write_component();
void lora_send();


/*
  ALL-TIME FUNCTIONS
*/

/*
    Inicia o display OLED
    - Inicia I2C nos pinos definidos
    - Inicia o OLED
*/
bool display_init() {
  if (!display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDR)) {
    Serial.println("[LoRa Receiver] Falha ao inicializar OLED");
    return false;
  }
  Serial.println("[LoRa Receiver] OLED inicializado");
  display.clearDisplay();
  display.setTextSize(2);
  display.setTextColor(WHITE);
  return true;
}

/*
    Inicia comunicação com chip LoRa
    - Inicia SPI
    - Inicia LoRa
*/
bool lora_chip_init() {
  Serial.println("[LoRa Sender] Iniciando LoRa...");
  SPI.begin(SCK_LORA, MISO_LORA, MOSI_LORA, SS_PIN_LORA);
  LoRa.setPins(SS_PIN_LORA, RESET_PIN_LORA, LORA_DEFAULT_DIO0_PIN);
  if (!LoRa.begin(BAND)) {
    Serial.println("[LoRa Sender] Falha no LoRa, retry em 1s");
    return false;
  }
  LoRa.setTxPower(HIGH_GAIN_LORA);
  Serial.println("[LoRa Sender] LoRa OK");
  return true;
}

/*
    Lê o valor do sensor de umidade do solo
    - Converte o valor para porcentagem
*/
float sensor_read() {
  float raw = analogRead(SENSOR_PIN);
  float perc = map(raw, VALOR_SECO, VALOR_UMIDO, 0, 100);
  float umidade_perc = constrain(perc, 0, 100);
  return umidade_perc;
}


/*
    Verifica se existe central
    - Realiza Handshake com a central
*/
void central_check() {
  last_central_check = millis();
  central_exists = false;
  LoRa.beginPacket(); LoRa.print("PING_CENTRAL"); LoRa.endPacket();
  Serial.printf("\n\n[LoRa] Checking for central...\n");
  unsigned long start = millis();
  while (millis() - start < TIMEOUT_MS) {
    if (LoRa.parsePacket()) {
      String resp = LoRa.readString();
      if (resp == "ACK_CENTRAL") {
        central_exists = true;
        break;
      }
    }
  }
  if (central_exists){
    if (!central_exists_aux) turn_into_component();
    central_exists_aux = true;
    Serial.printf("[LoRa] Central found!\n\n");
  }
  else{
    if (central_exists_aux)  turn_into_central();
    central_exists_aux = false;
    Serial.printf("[LoRa] No central found.\n\n");
  }
}

/*
  Configura o tempo de sono via NTP
  - Calcula o tempo de sono
  - Coloca o ESP32 em sono profundo (going_to_sleep)
*/
void set_sleep_time(){
  
  configTime(gmtOffset, daylightOffset, ntpServer);
  Serial.println("Sincronizando hora NTP...");
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo, 5000)) {
    Serial.println("Falha ao obter hora! Entrando em deep sleep curto...");
    return;
  }

  char buffer[64];
  strftime(buffer, sizeof(buffer), "%d/%m/%Y %H:%M:%S", &timeinfo);
  Serial.print("Hora atual: ");
  Serial.println(buffer);

  int now_h = timeinfo.tm_hour;
  int now_m = timeinfo.tm_min;
  int now_s = timeinfo.tm_sec;
  
  // Turn 'on' 8h and 'off' 18h
  if (now_h < 8) {
    // antes das 8h dorme até as 8h
    sleep_time_sec = ((8 - now_h) * 3600L) - ((now_m * 60L) - now_s);
  }
  else if (now_h < 18) {
    // entre 8 e 18h dorme ate as proximas 2h (partindo de 8h)
    sleep_time_sec = (now_h%2) * (60L - now_m) * 60L + now_s;
  }
  else {
    // depois das 18h dorme até as 8h do dia seguinte
    sleep_time_sec = ((24 - now_h + 8) * 3600L) - ((now_m * 60L) - now_s);
  }

  Serial.print("Dormindo por (segundos): ");
  Serial.println(sleep_time_sec);
  esp_sleep_enable_timer_wakeup(sleep_time_sec * uS_TO_S_FACTOR);
  going_to_sleep();
}

/*
    Coloca o ESP32 em modo de sono profundo
    - Desliga o LoRa
*/
void going_to_sleep() {
  Serial.println("[LoRa Sender] Indo para o sono profundo...");
  display.clearDisplay();
  display.setTextSize(2);
  display.setCursor(0, OLED_LINE1);
  display.printf("DORMINDO");
  display.display();
  delay(1000);

  display.clearDisplay();
  Serial.flush();
  esp_deep_sleep_start();
}

/*
  AS-CENTRAL FUNCTIONS
*/
/*
    Torna o dispositivo em central
    - Conecta ao WiFi
    - Inicia o servidor
*/
void turn_into_central(){
  wifi_connect();
  server_init();
}


/*
    Inicia o servidor
    - Monta o sistema de arquivos LittleFS
    - Define as rotas do servidor
*/
void server_init() {
  if (!LittleFS.begin()) {
    Serial.println("Erro ao montar LittleFS");
    return;
  }
  
  server.begin();
  // disponibiliza o url "/" com o conteúdo da String abaixo
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send(LittleFS, "/index.html", "text/html");
  });

  //  salva umidade no caminho "/umidade"
  server.on("/umidade", HTTP_GET, [](AsyncWebServerRequest *request) {
    char buf[16];
    dtostrf(data.umidade, 6, 2, buf);
    request->send(200, "text/plain", buf);
  });

  Serial.println("[LoRa Sender] Servidor HTTP iniciado");
}


/*
    Conecta ao WiFi
    - Configura IP estático
*/
void wifi_connect() {
  WiFi.mode(WIFI_STA);
  /*if (!WiFi.config(local_IP, gateway, subnet)) {
    Serial.println("Falha ao configurar IP estático"); 
  }
  */

  WiFi.begin(ssid, password);
  display_write_wifi();

  display.fillRect(0, OLED_LINE2, SCREEN_WIDTH, 10, BLACK);
  display.setCursor(0, OLED_LINE2);
  display.print("Conectando");
  display.display();

  int dotCount = 0;

  while (WiFi.status() != WL_CONNECTED) {
    display.setCursor(60, OLED_LINE2);
    for (int i = 0; i < 3; i++) {
      display.print(".");
      display.display();
      delay(300);
    }

    display.fillRect(60, OLED_LINE2, SCREEN_WIDTH - 60, 10, BLACK);
    display.display();

    for (int i = 0; i < 20; i++) Serial.println();
    Serial.println("Tentando conectar ao WiFi...");
  }

  display.fillRect(0, OLED_LINE2, SCREEN_WIDTH, 10, BLACK);
  display.setCursor(0, OLED_LINE2);
  display.print("Conectado");
  display.display();

  Serial.printf("\nConectado ao WiFi %s com IP: ", ssid);
  Serial.println(WiFi.localIP());  

}

/*
    Atualiza display sendo central
*/
void display_write_wifi() {
  display.clearDisplay();
  display.setTextSize(2);
  display.setCursor(0, OLED_LINE1);
  display.printf("EMISSOR");
  display.setTextSize(1);
  display.setCursor(0, OLED_LINE2);
  display.printf("Tentando conexao wifi");
  display.setCursor(0, OLED_LINE3);
  display.print("Rede: ");
  display.print(ssid);
  display.display();
}

/*
    Atualiza display sendo central
*/
void display_write_central() {
  display.clearDisplay();
  display.setTextSize(2);
  display.setCursor(0, OLED_LINE1);
  display.printf("EMISSOR");
  display.setTextSize(1);
  display.setCursor(0, OLED_LINE2);
  display.printf("Umidade: %.2f%%", data.umidade);
  display.setCursor(0, OLED_LINE3);
  display.print("IP: ");
  display.print(WiFi.localIP());
  display.display();
}


/*
  AS-COMPONENT FUNCTIONS
*/
/*
    Torna o dispositivo em componente
    - Desconecta do WiFi
    - Desliga o servidor
*/
void turn_into_component(){
  server.end();
  WiFi.disconnect();
  WiFi.mode(WIFI_OFF);
}


/*
    Atualiza display sendo componente
*/
void display_write_component() {
  display.clearDisplay();
  display.setTextSize(2);
  display.setCursor(0, OLED_LINE1);
  display.printf("COMPONENTE");
  display.setTextSize(1);
  display.setCursor(0, OLED_LINE2);
  display.printf("Umidade: %.2f%%", data.umidade);
  display.display();
}


/*
    Envia os dados via LoRa
    - Envia o pacote com os dados do sensor
*/
void lora_send() {
  LoRa.beginPacket();
  LoRa.write((uint8_t*)&data, sizeof(data));
  LoRa.endPacket();
}

/*


*/
void setup() {
  Serial.begin(DEBUG_SERIAL_BAUDRATE);
  pinMode(SENSOR_PIN, INPUT);
  Wire.begin(OLED_SDA_PIN, OLED_SCL_PIN);

  while (!display_init()) delay(500);

  while (!lora_chip_init()) delay(1000);

  central_check();
  if (central_exists) turn_into_component();
  else if (!central_exists) turn_into_central();

  delay(1500);
}

void loop() {

  if (millis() - last_central_check > TIMEOUT_GLOBAL_MS) central_check();

  data.umidade = sensor_read();

  if (central_exists){
    display_write_component();
    lora_send();
  }else{
    display_write_central();
  }

  delay(1500);

  set_sleep_time();
}


