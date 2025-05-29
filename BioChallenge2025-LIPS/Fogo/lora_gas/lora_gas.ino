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
#define HIGH_GAIN_LORA  14      // 0 ~ 20dBm
#define BAND            915E6 // 915MHz

// Pinos dos Sensores
#define GAS_SENSOR_PIN  36 // Pino Analógico A0 para sensor de gás (ex: MQ-2)
#define FLAME_SENSOR_PIN 2 // Pino Digital D2 para sensor de chama

// Definições do OLED
#define OLED_SDA_PIN    4
#define OLED_SCL_PIN    15
#define SCREEN_WIDTH    128
#define SCREEN_HEIGHT   64
#define OLED_ADDR       0x3C
#define OLED_RESET      16

// Linhas do OLED
#define OLED_LINE1      0
#define OLED_LINE2      20 // Ajustado para espaçamento
#define OLED_LINE3      35 // Ajustado para espaçamento
#define OLED_LINE4      50 // Para IP ou status adicional

// Identificadores
#define DEBUG_SERIAL_BAUDRATE 115200
#define TIMEOUT_HANDSHAKE 2000 // Tempo de espera Handshake
#define TIMEOUT_CENTRAL_CHECK 5000 // Espaçamento entre buscas por central

#define uS_TO_S_FACTOR 1000000ULL  // Fator de conversão para micro segundos para segundos
const long DEFAULT_SHORT_SLEEP_SECONDS = 300L; // 5 minutos para fallback do NTP

// Struct para dados LoRa (compatível com GasData no receptor)
struct GasEmitterData {
  float gas_level;    // Valor do sensor de gás (pode ser ADC raw, PPM, etc.)
  float flame_status; // 1.0 para chama detectada, 0.0 para não detectada
} gas_flame_data;

// Variáveis globais
bool central_exists;
bool central_exists_aux;
unsigned long last_central_check;

// WiFi
const char* ssid     = "Lips"; // Substitua pelo seu SSID
const char* password = "sala3086"; // Substitua pela sua senha

// Parâmetros de rede para ter IP estático (se emissor atuar como central)
#define ACCESS_POINT_IP_SUFFIX 66 // Sufixo do IP se este dispositivo for o ponto de acesso/central
IPAddress local_IP(192,168,0,ACCESS_POINT_IP_SUFFIX); // Ex: 192.168.0.66
IPAddress gateway(192,168,0,1);    // Gateway da sua rede
IPAddress subnet(255,255,255,0);   // Máscara de sub-rede

// Server declaration
AsyncWebServer server(80);

// Display setting
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

// NTP (para a lógica de sono)
const char* ntpServer  = "pool.ntp.org";
const long  gmtOffset  = -3 * 3600; // GMT-3 (Brasilia)
const int   daylightOffset = 0;     // Sem horário de verão
long sleep_time_sec;

// Protótipos de Funções
bool display_init();
bool lora_chip_init();
void read_sensors();
void central_check();
void set_sleep_time_and_sleep(); // Renomeado para clareza
void go_to_deep_sleep(); // Renomeado para clareza

void turn_into_central_role();
void server_init();
void wifi_connect_as_central();
void display_write_wifi_connecting();
void display_write_central_info();

void turn_into_component_role();
void display_write_component_info();
void lora_send_data();
String construct_gasflame_json();


/*
  Funções de Configuração e Utilitárias
*/
bool display_init() {
  if (!display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDR)) {
    Serial.println(F("[Emissor G/F] Falha ao inicializar OLED"));
    return false;
  }
  Serial.println(F("[Emissor G/F] OLED inicializado"));
  display.clearDisplay();
  display.setTextSize(1); // Tamanho padrão menor para mais info
  display.setTextColor(WHITE);
  display.setCursor(0,0);
  display.println(F("Emissor Gas/Fogo"));
  display.println(F("Iniciando..."));
  display.display();
  return true;
}

bool lora_chip_init() {
  Serial.println(F("[Emissor G/F] Iniciando LoRa..."));
  SPI.begin(SCK_LORA, MISO_LORA, MOSI_LORA, SS_PIN_LORA);
  LoRa.setPins(SS_PIN_LORA, RESET_PIN_LORA, LORA_DEFAULT_DIO0_PIN);
  if (!LoRa.begin(BAND)) {
    Serial.println(F("[Emissor G/F] Falha no LoRa, tentando novamente em 1s"));
    return false;
  }
  LoRa.setTxPower(HIGH_GAIN_LORA);
  Serial.println(F("[Emissor G/F] LoRa OK"));
  return true;
}

void read_sensors() {
  // Leitura do Sensor de Gás (Analógico)
  int raw_gas_value = analogRead(GAS_SENSOR_PIN);
  gas_flame_data.gas_level = (float)raw_gas_value; // Envia o valor ADC raw. Pode ser calibrado/convertido para PPM.
  Serial.printf("[Sensor] Gas (raw ADC): %.0f\n", gas_flame_data.gas_level);

  // Leitura do Sensor de Chama (Digital)
  bool flame_raw_status = digitalRead(FLAME_SENSOR_PIN); // HIGH ou LOW
  // Assumindo que o sensor de chama envia LOW quando detecta chama (comum em módulos KY-026)
  // Se o seu sensor envia HIGH ao detectar chama, inverta a lógica: flame_raw_status == HIGH
  if (flame_raw_status == LOW) {
    gas_flame_data.flame_status = 1.0; // Chama detectada
    Serial.println(F("[Sensor] Chama DETECTADA!"));
  } else {
    gas_flame_data.flame_status = 0.0; // Nenhuma chama detectada
    Serial.println(F("[Sensor] Nenhuma chama detectada."));
  }
}

String construct_gasflame_json() {
  String json_str = "{";
  json_str += "\"gas_level\":" + String(gas_flame_data.gas_level, 0) + ","; // Sem casas decimais para raw ADC
  json_str += "\"flame_status\":" + String(gas_flame_data.flame_status, 1);
  json_str += "}";
  return json_str;
}

/*
  Lógica de Papel Central/Componente e LoRa
*/
void central_check() {
  last_central_check = millis();
  central_exists = false;
  Serial.println(F("\n[LoRa] Verificando existencia de central..."));
  LoRa.beginPacket(); LoRa.print("PING_CENTRAL"); LoRa.endPacket();
  
  unsigned long start_time = millis();
  while (millis() - start_time < TIMEOUT_HANDSHAKE) {
    if (LoRa.parsePacket()) {
      String response = LoRa.readString();
      if (response == "ACK_CENTRAL") {
        central_exists = true;
        Serial.println(F("[LoRa] Central encontrada!"));
        break;
      }
    }
  }

  if (central_exists) {
    if (!central_exists_aux) { // Se antes não existia central, e agora existe
      turn_into_component_role();
    }
    central_exists_aux = true;
  } else {
    Serial.println(F("[LoRa] Nenhuma central encontrada."));
    if (central_exists_aux) { // Se antes existia central, e agora não existe
      turn_into_central_role(); // Assume papel de central
    }
    central_exists_aux = false;
  }
}

void lora_send_data() {
  Serial.println(F("[LoRa] Enviando dados de Gas/Chama..."));
  LoRa.beginPacket();
  LoRa.write((uint8_t*)&gas_flame_data, sizeof(gas_flame_data));
  LoRa.endPacket();
  Serial.println(F("[LoRa] Dados enviados."));
}

/*
  Funções para Papel de Componente
*/
void turn_into_component_role() {
  Serial.println(F("[Sistema] Assumindo papel de COMPONENTE."));
  server.end(); // Desliga servidor web se estava ativo
  if (WiFi.isConnected()) {
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);
    Serial.println(F("[WiFi] Desconectado (modo componente)."));
  }
  display_write_component_info();
}

void display_write_component_info() {
  display.clearDisplay();
  display.setTextSize(1);
  display.setCursor(0, OLED_LINE1);
  display.println(F("COMPONENTE Gas/Fogo"));
  display.setCursor(0, OLED_LINE2);
  display.printf("Gas: %.0f\n", gas_flame_data.gas_level);
  display.setCursor(0, OLED_LINE3);
  display.printf("Chama: %s\n", gas_flame_data.flame_status == 1.0 ? "DETECTADA!" : "Nao");
  display.setCursor(0, OLED_LINE4);
  display.println(F("Enviando LoRa..."));
  display.display();
}

/*
  Funções para Papel de Central
*/
void turn_into_central_role() {
  Serial.println(F("[Sistema] Assumindo papel de CENTRAL."));
  // Não há necessidade de desconectar WiFi se já está tentando ser central
  wifi_connect_as_central(); // Conecta ao WiFi e inicia servidor
  display_write_central_info();
}

void wifi_connect_as_central() {
  if (WiFi.isConnected()) {
    Serial.println(F("[WiFi] Ja conectado."));
    if (!server.count()) server_init(); // Garante que o servidor está rodando
    return;
  }

  WiFi.mode(WIFI_STA);
  // WiFi.config(local_IP, gateway, subnet); // Configura IP estático
  display_write_wifi_connecting();

  WiFi.begin(ssid, password);
  unsigned long wifi_connect_start = millis();
  while (WiFi.status() != WL_CONNECTED) {
    if (millis() - wifi_connect_start > 15000) { // Timeout de 15s
      Serial.println(F("[WiFi] Falha ao conectar WiFi - Timeout!"));
      display.clearDisplay();
      display.setCursor(0,0);
      display.println(F("Falha WiFi!"));
      display.display();
      delay(2000);
      // Poderia tentar modo AP aqui ou dormir
      return; 
    }
    Serial.print(".");
    delay(500);
  }
  Serial.println(F("\n[WiFi] Conectado!"));
  Serial.print(F("[WiFi] Endereco IP: "));
  Serial.println(WiFi.localIP());
  server_init(); // Inicia o servidor web após conectar
}

void server_init() {
  if (!LittleFS.begin()) {
    Serial.println(F("Erro ao montar LittleFS"));
    return;
  }
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
    if(LittleFS.exists("/index.html")){
        request->send(LittleFS, "/index.html", "text/html");
    } else {
        request->send(200, "text/plain", "Index.html nao encontrado.");
    }
  });
  server.on("/gasflame", HTTP_GET, [](AsyncWebServerRequest *request) { // Endpoint para dados de gas/chama
    String json_payload = construct_gasflame_json();
    request->send(200, "application/json", json_payload);
  });
  server.begin();
  Serial.println(F("[HTTP] Servidor iniciado."));
}

void display_write_wifi_connecting() {
  display.clearDisplay();
  display.setTextSize(1);
  display.setCursor(0, OLED_LINE1);
  display.println(F("CENTRAL Gas/Fogo"));
  display.setCursor(0, OLED_LINE2);
  display.println(F("Conectando WiFi..."));
  display.setCursor(0, OLED_LINE3);
  display.print(F("Rede: "));
  display.println(ssid);
  display.display();
}

void display_write_central_info() {
  display.clearDisplay();
  display.setTextSize(1);
  display.setCursor(0, OLED_LINE1);
  display.println(F("CENTRAL Gas/Fogo"));
  display.setCursor(0, OLED_LINE2);
  display.printf("Gas: %.0f\n", gas_flame_data.gas_level);
  display.setCursor(0, OLED_LINE3);
  display.printf("Chama: %s\n", gas_flame_data.flame_status == 1.0 ? "DETECTADA!" : "Nao");
  display.setCursor(0, OLED_LINE4);
  if (WiFi.isConnected()) {
    display.print(F("IP: "));
    display.println(WiFi.localIP());
  } else {
    display.println(F("WiFi Nao Conectado"));
  }
  display.display();
}

/*
  Lógica de Sono
*/
void set_sleep_time_and_sleep() {
  // Se for componente, não precisa de WiFi para NTP, pode ter sono fixo ou mais simples
  // Se for central, pode usar NTP.
  // Para simplificar este exemplo de emissor, vamos usar um sono fixo curto
  // se a lógica NTP falhar ou se for componente.

  if (!central_exists) { // Se é central, tenta NTP
    if (!WiFi.isConnected()) {
        Serial.println(F("[Sleep] WiFi Nao conectado para NTP. Sono curto padrao."));
        sleep_time_sec = DEFAULT_SHORT_SLEEP_SECONDS;
    } else {
        configTime(gmtOffset, daylightOffset, ntpServer);
        Serial.println(F("[Sleep] Sincronizando hora NTP..."));
        struct tm timeinfo;
        if (!getLocalTime(&timeinfo, 10000)) { // Timeout 10s
            Serial.println(F("[Sleep] Falha ao obter hora NTP! Sono curto padrao."));
            sleep_time_sec = DEFAULT_SHORT_SLEEP_SECONDS;
        } else {
            char time_buffer[30];
            strftime(time_buffer, sizeof(time_buffer), "%H:%M:%S", &timeinfo);
            Serial.printf("[Sleep] Hora atual NTP: %s\n", time_buffer);

            int now_h = timeinfo.tm_hour;
            int now_m = timeinfo.tm_min;
            int now_s = timeinfo.tm_sec;

            // Lógica de sono: Acordado das 08:00 às 18:00, envia a cada ~2 horas.
            // Fora desse horário, dorme até as 08:00.
            if (now_h < 8) { // Antes das 8h
                sleep_time_sec = ((8 - now_h) * 3600L) - (now_m * 60L) - now_s;
            } else if (now_h < 18) { // Entre 8h e 18h
                // Dorme até a próxima marca de 2 horas (ex: se 8:30, dorme até 10:00)
                long seconds_past_last_even_hour_block = (now_h % 2 == 0) ? (now_m * 60L + now_s) : ((now_m + 60) * 60L + now_s) ;
                sleep_time_sec = (2 * 3600L) - seconds_past_last_even_hour_block;
                 if (now_h % 2 != 0) sleep_time_sec = (1 * 3600L) - (now_m * 60L + now_s); // se hora impar, dorme ate a proxima hora
                 else sleep_time_sec = (2*3600L) - (now_m*60L + now_s); // se hora par, dorme 2h
            } else { // Depois das 18h
                sleep_time_sec = ((24 - now_h + 8) * 3600L) - (now_m * 60L) - now_s;
            }

            if (sleep_time_sec <= 0) { // Segurança
                 sleep_time_sec = 120; // Dorme por 2 minutos se cálculo der errado
            }
        }
    }
  } else { // É componente, sono fixo mais curto
      sleep_time_sec = 60; // Componente envia a cada 1 minuto (exemplo)
  }
  
  Serial.printf("[Sleep] Indo dormir por %ld segundos.\n", sleep_time_sec);
  esp_sleep_enable_timer_wakeup(sleep_time_sec * uS_TO_S_FACTOR);
  go_to_deep_sleep();
}

void go_to_deep_sleep() {
  Serial.println(F("[Sistema] Entrando em sono profundo..."));
  display.clearDisplay();
  display.setCursor(0,0);
  display.setTextSize(2);
  display.println(F("DORMINDO"));
  display.display();
  delay(1000);

  LoRa.sleep(); // Coloca LoRa em modo de baixo consumo
  display.ssd1306_command(SSD1306_DISPLAYOFF); // Desliga display
  WiFi.disconnect(true); // Desconecta e desliga WiFi radio
  WiFi.mode(WIFI_OFF);

  Serial.flush();
  esp_deep_sleep_start();
}


/*
  Função setup()
*/
void setup() {
  Serial.begin(DEBUG_SERIAL_BAUDRATE);
  Serial.println(F("\n[Emissor G/F] Iniciando Setup..."));

  pinMode(GAS_SENSOR_PIN, INPUT);
  pinMode(FLAME_SENSOR_PIN, INPUT_PULLUP); // Usar PULLUP se o sensor de chama flutuar em HIGH

  Wire.begin(OLED_SDA_PIN, OLED_SCL_PIN);
  if (!display_init()) {
      Serial.println(F("Display OLED FALHOU! Loop infinito."));
      while(1);
  }

  if (!lora_chip_init()) {
      display.clearDisplay(); display.setCursor(0,0); display.println(F("LoRa FALHOU!")); display.display();
      Serial.println(F("Chip LoRa FALHOU! Loop infinito."));
      while(1);
  }
  
  // Inicializa dados com valores padrão
  gas_flame_data.gas_level = 0.0;
  gas_flame_data.flame_status = 0.0;

  // Verifica o papel inicial (componente ou central)
  // Por padrão, começa como componente e verifica se há central.
  // Se não houver, o primeiro central_check() o tornará central.
  central_exists = true; // Assume que existe para forçar a ser componente inicialmente
  central_exists_aux = true; 
  central_check(); // Primeira verificação para definir o papel

  Serial.println(F("[Emissor G/F] Setup completo."));
  delay(1000);
}

/*
  Função loop()
*/
void loop() {
  if (millis() - last_central_check > TIMEOUT_CENTRAL_CHECK) {
    central_check(); // Verifica periodicamente o papel
  }

  read_sensors(); // Lê os sensores

  if (central_exists) { // Papel de Componente
    display_write_component_info();
    lora_send_data();
  } else { // Papel de Central
    display_write_central_info();
    // Como central, ele não envia LoRa por padrão, apenas serve dados via HTTP
    // e responde a PINGs (já tratado em central_check implicitamente pelo receptor)
    // Se a central também precisasse ENVIAR dados LoRa, seria adicionado aqui.
  }

  delay(2000); // Pequeno delay no loop principal antes de decidir dormir

  set_sleep_time_and_sleep(); // Configura o tempo de sono e dorme
}
