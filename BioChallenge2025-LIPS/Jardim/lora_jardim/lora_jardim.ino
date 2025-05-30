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

// Pino do sensor de umidade do solo
const int sensor_pins[] = {36, 37};
#define NUM_SENSORS (sizeof(sensor_pins) / sizeof(sensor_pins[0]))
 
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

#define TIMEOUT_HANDSHAKE 2000 // Tempo de espera Handshake
#define TIMEOUT_CENTRAL_CHECK 5000 // Espaçamento entre buscas por central

#define uS_TO_S_FACTOR 1000000ULL  // Conversion factor for micro seconds to seconds

// PRECISA CALIBRAR!
#define VALOR_SECO  2400 // Valor ADC para solo seco
#define VALOR_UMIDO 1000 // Valor ADC para solo úmido
// Threshold para acionamento do motor (em porcentagem de umidade)
#define UMIDADE_MINIMA_PARA_MOTOR VALOR_SECO // Abaixo deste valor, aciona o motor (ex: irrigar)

// Definição dos pinos de controle do motor
const int a1 = 38; // Motor 1 FWD
const int b2 = 39; // Motor 1 REV (ou outra lógica dependendo do driver)
const int b1 = 34; // Motor 2 FWD
const int a2 = 35; // Motor 2 REV (ou outra lógica dependendo do driver)


//Struct para dados do LoRa
struct LoraData {
  float umidade[NUM_SENSORS];
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
IPAddress local_IP(192,168,ACESS_POINT,65);
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
const long DEFAULT_SHORT_SLEEP_SECONDS = 300L; // 5 minutos para fallback do NTP

int bootCount = 0; // Contador de reinicializações

// all-time functions
bool display_init();
bool lora_chip_init();''
void sensor_read();
void central_check();
void set_sleep_time();
void going_to_sleep();
String construct_humidity_json(); // Declaração da nova função

// as-central functions
void turn_into_central();
void server_init();
void wifi_connect();
void display_write_wifi();
void display_write_central();

// as-component functions
void turn_into_component();
void display_write_component();
void lora_send();

// Motor control functions (declarations)
void descer1();
void subir1();
void descer2();
void subir2();
void reset_motors(); // Renomeado de reset para evitar conflito com OLED_RESET define


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
    - Reads all sensors defined in sensor_pins[]
    - Stores -1.0 if sensor seems disconnected/faulty (raw ADC at 0 or 4095)
    - Aciona a lógica do motor com base nas leituras dos sensores
*/
void sensor_read() {
  for (int i = 0; i < NUM_SENSORS; i++) {
    float raw = analogRead(sensor_pins[i]);
    // Checa se pino esta desconectado (PULL_UP/DOWN)
    if (raw <= VALOR_UMIDO) { 
      data.umidade[i] = -1.0; // Indica sensor não conectado ou com falha
      Serial.printf("[Sensor %d] Leitura invalida ou desconectado. Raw: %.0f\n\n", i, raw);
    } else {
      float perc = map(raw, VALOR_SECO, VALOR_UMIDO, 0, 100);
      data.umidade[i] = constrain(perc, 0, 100);
      Serial.printf("[Sensor %d] Raw: %.0f, Umidade: %.2f%%\n\n", i, raw, data.umidade[i]);
    }
  }


  // Lógica de acionamento do Motor 1 (baseado no sensor 0)
  if (NUM_SENSORS > 0) {
    if (data.umidade[0] != -1.0 && data.umidade[0] < UMIDADE_MINIMA_PARA_MOTOR) {
      Serial.printf("[Motor 1] Umidade S0 (%.2f%%) baixa. Acionando subir1().\n", data.umidade[0]);
      subir1(); // Assume 'subir' = irrigar ou ação para baixa umidade
    } else {
      // Se umidade OK ou sensor com falha, aciona 'descer1' (parar irrigação ou estado seguro)
      Serial.printf("[Motor 1] Umidade S0 (%.2f%%) OK ou N/A. Acionando descer1().\n", data.umidade[0]);
      descer1(); 
    }
  }

  // Lógica de acionamento do Motor 2 (baseado no sensor 1)
  if (NUM_SENSORS > 1) {
    if (data.umidade[1] != -1.0 && data.umidade[1] < UMIDADE_MINIMA_PARA_MOTOR) {
      Serial.printf("[Motor 2] Umidade S1 (%.2f%%) baixa. Acionando subir2().\n", data.umidade[1]);
      subir2();
    } else {
      Serial.printf("[Motor 2] Umidade S1 (%.2f%%) OK ou N/A. Acionando descer2().\n", data.umidade[1]);
      descer2();
    }
  }
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
  while (millis() - start < TIMEOUT_HANDSHAKE) {
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
    if (central_exists_aux || bootCount == 1)  turn_into_central();
    central_exists_aux = false;
    Serial.printf("[LoRa] No central found.\n\n");
  }

}

/*
  Configura o tempo de sono via NTP
  - Calcula o tempo de sono
  - Coloca o ESP32 em sono profundo (going_to_sleep)
  - Em caso de falha NTP, dorme por um tempo curto padrão.
*/
void set_sleep_time(){
  
  struct tm timeinfo;

  wifi_connect(); // Tenta conectar ao WiFi para NTP
  configTime(gmtOffset, daylightOffset, ntpServer); // Configura a hora de acordo com paramentros

  if (!getLocalTime(&timeinfo, 5000)) { // Aumentado timeout NTP para 5s
    Serial.println("Falha ao obter hora NTP! Entrando em deep sleep curto...");
    esp_sleep_enable_timer_wakeup(DEFAULT_SHORT_SLEEP_SECONDS * uS_TO_S_FACTOR);
    going_to_sleep();
    return; // Não continua se NTP falhar
  }

  Serial.println("Sincronizando hora NTP..."); // Movido para depois da checagem de sucesso
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
    sleep_time_sec = ((8 - now_h) * 3600L) - (now_m * 60L) - now_s;
  }
  else if (now_h < 18) {
    // entre 8 e 18h dorme ate as proximas 2h (partindo de 8h)
    long seconds_into_current_hour = now_m * 60L + now_s;
    long seconds_to_next_even_hour_block_start = ( ( (now_h / 2) + 1) * 2 * 3600L ); // Ex: se 9h, (4+1)*2*3600 = 10*3600 (10:00)
                                                                                    // se 8h, (4+1)*2*3600 = 10*3600 (10:00)
                                                                                    // se 10h, (5+1)*2*3600 = 12*3600 (12:00)
    long current_seconds_from_midnight = now_h * 3600L + now_m * 60L + now_s;
    
    // Calcula o próximo horário par (8:00, 10:00, 12:00, 14:00, 16:00, 18:00)
    // Se agora é 8:xx, próximo é 10:00. Se 9:xx, próximo é 10:00.
    // Se agora é 17:xx, próximo é 18:00.
    long next_target_hour = ((now_h / 2) + 1) * 2;
    if (next_target_hour > 18) next_target_hour = 18; // Limita a 18:00
    
    sleep_time_sec = (next_target_hour * 3600L) - current_seconds_from_midnight;


  }
  else {
    // depois das 18h dorme até as 8h do dia seguinte
    sleep_time_sec = ((24 - now_h + 8) * 3600L) - (now_m * 60L) - now_s; 
  }

  if (sleep_time_sec <= 0) { // Se cálculo resultar em tempo negativo ou zero (ex: já passou do target)
      if (now_h < 18 && now_h >=8) { // Se durante o dia, dorme por 2h menos o que já passou da hora atual
          sleep_time_sec = (2*3600L) - (now_m*60L + now_s);
           if (sleep_time_sec <=0) sleep_time_sec = 300; // Mínimo 5 min se ainda negativo
      } else {
          sleep_time_sec = DEFAULT_SHORT_SLEEP_SECONDS; // Fallback para sono curto
      }
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

  LoRa.sleep(); // Coloca o LoRa em modo de baixo consumo
  display.clearDisplay(); 
  display.ssd1306_command(SSD1306_DISPLAYOFF); // Desliga o display OLED
  Serial.flush();
  esp_deep_sleep_start();
}

/*
  Constrói uma string JSON com as leituras de umidade.
*/
String construct_humidity_json() {
  String json_str = "{";
  for (int i = 0; i < NUM_SENSORS; i++) {
    json_str += "\"s" + String(i) + "\":";
    char val_buf[10];
    dtostrf(data.umidade[i], 5, 2, val_buf); // Formata float: min 5 chars, 2 decimais
    json_str += String(val_buf);
    if (i < NUM_SENSORS - 1) {
      json_str += ",";
    }
  }
  json_str += "}";
  return json_str;
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
  Serial.printf("\n[LoRa] Turning into Central\n");
  wifi_connect();
  server_init();
}


/*
    Inicia o servidor
    - Monta o sistema de arquivos LittleFS
    - Define as rotas do servidor, incluindo a rota /umidade para enviar todas as leituras em JSON
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

  // Envia todas as umidades no formato JSON
  server.on("/umidade", HTTP_GET, [](AsyncWebServerRequest *request) {
    String json_payload = construct_humidity_json();
    request->send(200, "application/json", json_payload);
  });

  Serial.println("[LoRa Sender] Servidor HTTP iniciado");
}


/*
    Conecta ao WiFi
    - Configura IP estático
*/
void wifi_connect() {
  if (WiFi.isConnected()) {
      Serial.println("WiFi ja conectado.");
      // Não precisa atualizar display aqui, será feito se for central
      return;
  }

  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  
  // Atualiza display apenas se for se tornar central (ou precisar de WiFi para NTP)
  // A chamada display_write_wifi() pode ser movida para turn_into_central se for apenas para modo central
  display_write_wifi(); // Mostra tentativa de conexão

  display.fillRect(0, OLED_LINE2, SCREEN_WIDTH, 10, BLACK);
  display.setCursor(0, OLED_LINE2);
  display.print("Conectando");
  display.display();

  unsigned long wifi_connect_timeout = millis();
  while (WiFi.status() != WL_CONNECTED) {
    if (millis() - wifi_connect_timeout > 15000) { // 15 segundos de timeout
        Serial.println("Falha ao conectar WiFi - Timeout!");
        display.fillRect(0, OLED_LINE2, SCREEN_WIDTH, 10, BLACK);
        display.setCursor(0, OLED_LINE2);
        display.print("Falha WiFi");
        display.display();
        return; 
    }
    display.setCursor(90, OLED_LINE2);
    for (int i = 0; i < 3; i++) {
      display.print(".");
      display.display();
      delay(300);
    }
    display.fillRect(90, OLED_LINE2, SCREEN_WIDTH - 90, 10, BLACK);
    display.display();
    delay(100);
  }

  display.fillRect(0, OLED_LINE2, SCREEN_WIDTH, 10, BLACK);
  display.setCursor(0, OLED_LINE2);
  display.print("Conectado");
  display.display();

  Serial.printf("\nConectado ao WiFi %s com IP: ", ssid);
  Serial.println(WiFi.localIP());  
}

/*
    Atualiza display sendo central (durante conexão WiFi)
*/
void display_write_wifi() {
  display.clearDisplay();
  display.setTextSize(2);
  display.setCursor(0, OLED_LINE1);
  display.printf("CENTRAL");
  display.setTextSize(1);
  display.setCursor(0, OLED_LINE2);
  display.printf("WiFi Conexao");
  display.setCursor(0, OLED_LINE3);
  display.print("Rede: ");
  display.print(ssid);
  display.display();
}

/*
    Atualiza display sendo central (após conectado e operando)
*/
void display_write_central() {
  display.clearDisplay();
  display.setTextSize(2);
  display.setCursor(0, OLED_LINE1);
  display.printf("CENTRAL JD");
  display.setTextSize(1);
  display.setCursor(0, OLED_LINE2-10);
  for (int i = 0; i < NUM_SENSORS; i++) {
    display.setCursor(0, OLED_LINE2 + ((i-1) * 10));
    if (data.umidade[i] != -1.0) {
      display.printf("S%d: %.2f%%", i, data.umidade[i]);
    }else {
      display.printf("S%d: N/A", i);
    }
  }

  // Para mostrar mais sensores, precisaria de mais linhas ou paginação no display
  // Ex: if (NUM_SENSORS > 1) { display.setCursor(60, OLED_LINE2); display.printf("S2: %.2f%%", data.umidade[1]); }
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
  if (WiFi.isConnected()) {
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);
    Serial.println("WiFi Desconectado. Modo Componente.");
  }
}


/*
    Atualiza display sendo componente
*/
void display_write_component() {
  display.clearDisplay();
  display.setTextSize(2);
  display.setCursor(0, OLED_LINE1);
  display.printf("JARDIM");
  display.setTextSize(1);
  display.setCursor(0, OLED_LINE2);
  for (int i = 0; i < NUM_SENSORS; i++) {
    display.setCursor(0, OLED_LINE2 + ((i-1) * 10));
    if (data.umidade[i] != -1.0) {
      display.printf("S%d: %.2f%%", i + 1, data.umidade[i]);
    }else {
      display.printf("S%d: N/A", i + 1);
    }
  }
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
  Serial.println("Pacote LoRa enviado.");
}

/*


*/
void setup() {
  // Inicializa contagem de boot
  bootCount++;

  Serial.begin(DEBUG_SERIAL_BAUDRATE);
  Wire.begin(OLED_SDA_PIN, OLED_SCL_PIN);

  // Configura pinos dos motores como SAÍDA
  pinMode(a1, OUTPUT);
  pinMode(a2, OUTPUT);
  pinMode(b1, OUTPUT);
  pinMode(b2, OUTPUT);
  reset_motors(); // Garante que os motores comecem desligados
  
  // Configura pinos dos sensores como ENTRADA
  for (int i = 0; i < NUM_SENSORS; i++) {
    pinMode(sensor_pins[i], INPUT);
  }

  // Inicializa dados de umidade com valor de erro/N.A.
  for (int i = 0; i < NUM_SENSORS; i++) {
    data.umidade[i] = -1.0; 
  }

  while (!display_init()) delay(500);
  while (!lora_chip_init()) delay(1000);

  central_check(); // Determina o papel inicial (central ou componente)

  delay(1500);
}

void loop() {
  if (millis() - last_central_check > TIMEOUT_CENTRAL_CHECK) {
    central_check(); // Verifica periodicamente a presença da central e ajusta o papel
  }

  sensor_read(); // Lê sensores e aciona lógica de motores

  if (central_exists){ // Se é componente (central existe)
    display_write_component();
    lora_send();
  } else { // Se é a central (central não existe, então EU sou a central)
    display_write_central();
  }

  delay(1500);

  set_sleep_time(); // Configura e entra em modo de sono profundo
}


// Funções de controle do motor
// IMPORTANTE: A lógica exata (HIGH/LOW para a1,b1,a2,b2) depende do seu driver de motor (ex: L298N)
// As funções abaixo assumem uma configuração. Ajuste conforme necessário.

void descer1(){ // Ex: Parar irrigação Motor 1
  Serial.println("Motor 1: Acao 'descer1'");
  reset_motors(); // Para garantir, desliga todos antes de uma nova ação
  // Lógica para "descer" ou "parar" motor 1. Pode ser apenas desligar.
  digitalWrite(a1, LOW); 
  digitalWrite(b2, LOW); 
}

void subir1(){ // Ex: Iniciar irrigação Motor 1
  Serial.println("Motor 1: Acao 'subir1'");
  reset_motors();
  // Lógica para "subir" ou "acionar" motor 1 (ex: sentido horário)
  digitalWrite(a1, HIGH); // Exemplo: a1 HIGH, b2 LOW para um sentido
  digitalWrite(b2, LOW);
  delay(2000); // Mantém acionado por 2s
  reset_motors(); // Desliga após o tempo
}

void descer2(){ // Ex: Parar irrigação Motor 2
  Serial.println("Motor 2: Acao 'descer2'");
  reset_motors();
  digitalWrite(b1, LOW);
  digitalWrite(a2, LOW);

}

void subir2(){ // Ex: Iniciar irrigação Motor 2
  Serial.println("Motor 2: Acao 'subir2'");
  reset_motors();
  digitalWrite(b1, HIGH); // Exemplo: b1 HIGH, a2 LOW para um sentido
  digitalWrite(a2, LOW);
  delay(2000);
  reset_motors();
}

void reset_motors(){ // Renomeado para evitar conflito com OLED_RESET
  digitalWrite(a1, LOW);
  digitalWrite(b1, LOW);
  digitalWrite(a2, LOW);
  digitalWrite(b2, LOW);
}
