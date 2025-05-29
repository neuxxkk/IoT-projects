#include <WiFi.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <LittleFS.h>
#include <LoRa.h>
#include <SPI.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

// Pinos do LoRa
#define SCK_LORA        5
#define MISO_LORA       19
#define MOSI_LORA       27
#define RESET_PIN_LORA  14
#define SS_PIN_LORA     18

// Parâmetros LoRa
#define HIGH_GAIN_LORA  14      // 0 ~ 20dBm (Não usado diretamente pelo receptor, mas mantido)
#define BAND            915E6 // 915MHz

// Definicoes do OLED 
#define OLED_SDA_PIN    4
#define OLED_SCL_PIN    15
#define SCREEN_WIDTH    128 
#define SCREEN_HEIGHT   64  
#define OLED_ADDR       0x3C 
#define OLED_RESET      16

// Linhas do OLED - Ajustadas para mais informações
#define OLED_LINE_TITLE   0
#define OLED_LINE_UMID1   18
#define OLED_LINE_UMID2   30
#define OLED_LINE_GAS     42
#define OLED_LINE_IP      54


// Parâmetros de rede
#define ACESS_POINT 0 // Define se este dispositivo deve tentar ser um Access Point (0 para STA)

IPAddress local_IP(192,168,0,65); // IP estático desejado para este receptor
IPAddress gateway(192,168,0,1);   // Gateway da sua rede
IPAddress subnet(255,255,255,0);  // Máscara de sub-rede

// NUM_SENSORS para JardimData (umidade)
#define NUM_HUMIDITY_SENSORS 2 // Corresponde ao emissor de umidade

// Structs para Data Receiving
struct JardimData { // Para dados de umidade
  float umidade[NUM_HUMIDITY_SENSORS];
} jardim_data;

struct GasData { // Para dados de gás e chama
  float gas_level;    // Renomeado para corresponder ao emissor GasEmitterData
  float flame_status; // Renomeado para corresponder ao emissor GasEmitterData
} gas_data;


// Identificadores
#define DEBUG_SERIAL_BAUDRATE    115200

// WIFI
const char* ssid     = "Lips"; // Substitua pelo seu SSID
const char* password = "sala3086"; // Substitua pela sua senha

// Server declaration
AsyncWebServer server(80);
    
// Outros
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

// Flags para indicar recebimento de novos dados
bool new_jardim_data_received = false;
bool new_gas_data_received = false;

// Funções de inicialização
bool display_init(void);
bool lora_chip_init(void);
void server_init(void);

// Funções de primeiro contato
void wifi_connect(void);
void default_display_message(const char* msg); // Modificado para mensagem genérica

// Funcao de interpretação LoRa
bool lora_interpret_packet(int packetSize);

// Funções de leitura dos componentes
void read_jardim_data(void);
void read_gas_flame_data(void);

// Funções de escrita e envio
void update_oled_display(void); // Renomeado para clareza
void display_wifi_connecting_status(void); // Renomeado
String construct_jardim_data_json(void); // Renomeado
String construct_gas_flame_data_json(void);


/*
    Funcoes de inicialização
*/
bool display_init() {
  if (!display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDR)) {
    Serial.println(F("[Receptor] Falha ao inicializar OLED"));
    return false;
  }
  Serial.println(F("[Receptor] OLED inicializado"));
  display.clearDisplay();
  display.setTextColor(WHITE);
  // Mensagem inicial será mostrada em setup()
  return true;
}

bool lora_chip_init() {
  Serial.println(F("[Receptor] Iniciando LoRa..."));
  SPI.begin(SCK_LORA, MISO_LORA, MOSI_LORA, SS_PIN_LORA);
  LoRa.setPins(SS_PIN_LORA, RESET_PIN_LORA, LORA_DEFAULT_DIO0_PIN);

  if (!LoRa.begin(BAND)) {
    Serial.println(F("[Receptor] Falha no LoRa, tentando novamente em 1s"));
    return false;
  }
  LoRa.enableCrc(); // Boa prática para recepção de dados
  Serial.println(F("[Receptor] LoRa OK. Aguardando pacotes..."));
  return true;
}

String construct_jardim_data_json() {
  String json_str = "{";
  for (int i = 0; i < NUM_HUMIDITY_SENSORS; i++) {
    json_str += "\"s" + String(i) + "\":";
    char val_buf[10];
    dtostrf(jardim_data.umidade[i], 5, 2, val_buf);
    json_str += String(val_buf);
    if (i < NUM_HUMIDITY_SENSORS - 1) {
      json_str += ",";
    }
  }
  json_str += "}";
  return json_str;
}

String construct_gas_flame_data_json() {
  String json_str = "{";
  json_str += "\"gas_level\":" + String(gas_data.gas_level, 0) + ","; // Raw ADC como inteiro
  json_str += "\"flame_status\":" + String(gas_data.flame_status, 1); // 0.0 ou 1.0
  json_str += "}";
  return json_str;
}

void server_init(){
    server.begin();
    server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
        if(LittleFS.exists("/index.html")){
            request->send(LittleFS, "/index.html", "text/html");
        } else {
            request->send(200, "text/plain", "Index.html nao encontrado no LittleFS.");
        }
    });

    server.on("/umidade", HTTP_GET, [](AsyncWebServerRequest *request) {
        String json_payload = construct_jardim_data_json();
        request->send(200, "application/json", json_payload);
    });

    server.on("/gasflame", HTTP_GET, [](AsyncWebServerRequest *request) { // Novo endpoint
        String json_payload = construct_gas_flame_data_json();
        request->send(200, "application/json", json_payload);
    });

    Serial.println(F("[Receptor] Servidor HTTP iniciado"));
}

/*
    Funções de primeiro contato
*/
void wifi_connect() {
  WiFi.mode(WIFI_STA);
  if (!WiFi.config(local_IP, gateway, subnet)) {
    Serial.println(F("Falha ao configurar IP estático")); 
  }

  WiFi.begin(ssid, password);
  display_wifi_connecting_status();

  display.fillRect(0, OLED_LINE_UMID1 + 10, SCREEN_WIDTH, 10, BLACK); 
  display.setCursor(0, OLED_LINE_UMID1 + 10);
  display.setTextSize(1);
  display.print(F("Conectando WiFi"));
  display.display();

  unsigned long wifi_connect_start = millis();
  while (WiFi.status() != WL_CONNECTED) {
    if (millis() - wifi_connect_start > 15000) {
        Serial.println(F("[WiFi] Falha ao conectar - Timeout!"));
        display.fillRect(0, OLED_LINE_UMID1 + 10, SCREEN_WIDTH, 20, BLACK);
        display.setCursor(0, OLED_LINE_UMID1 + 10);
        display.print(F("Falha WiFi!"));
        display.display();
        delay(2000);
        return;
    }
    display.setCursor(90, OLED_LINE_UMID1 + 10); 
    for (int i = 0; i < 3; i++) {
      display.print(".");
      display.display();
      delay(300);
    }
    display.fillRect(90, OLED_LINE_UMID1 + 10, SCREEN_WIDTH - 90, 10, BLACK);
    display.display();
    delay(100);
    Serial.print(".");
  }
  Serial.println();

  display.fillRect(0, OLED_LINE_UMID1 + 10, SCREEN_WIDTH, 20, BLACK);
  display.setCursor(0, OLED_LINE_UMID1 + 10);
  display.print(F("WiFi Conectado!"));
  display.setCursor(0, OLED_LINE_UMID2 + 10);
  display.print(WiFi.localIP());
  display.display();

  Serial.printf("\n[WiFi] Conectado ao %s com IP: ", ssid);
  Serial.println(WiFi.localIP());  
  delay(2000); // Mostra status de conexão por um tempo
}

void display_wifi_connecting_status() {
  display.clearDisplay();
  display.setTextSize(2);
  display.setCursor(0, OLED_LINE_TITLE);
  display.print(F("RECEPTOR"));
  display.setTextSize(1);
  display.setCursor(0, OLED_LINE_UMID1); 
  display.print(F("WiFi: "));
  display.print(ssid);
  display.display();
}

void default_display_message(const char* msg) {
    display.clearDisplay();
    display.setTextSize(2); 
    display.setCursor(0, OLED_LINE_TITLE); 
    display.print(F("RECEPTOR"));
    display.setTextSize(1); 
    display.setCursor(0, OLED_LINE_UMID1 + 10); 
    display.print(msg);
    if(WiFi.isConnected()){
        display.setCursor(0, OLED_LINE_IP);
        display.print(F("IP: "));
        display.print(WiFi.localIP());
    }
    display.display();
}

/*
    Funcoes de leitura
*/
void read_jardim_data() {
    LoRa.readBytes((uint8_t*)&jardim_data, sizeof(JardimData));
    new_jardim_data_received = true;
    Serial.println(F("[LoRa] Dados de JardimData recebidos:"));
    for(int i=0; i<NUM_HUMIDITY_SENSORS; i++){
        Serial.printf("  Umidade S%d: %.2f%%\n", i, jardim_data.umidade[i]);
    }
}

void read_gas_flame_data() {
    LoRa.readBytes((uint8_t*)&gas_data, sizeof(GasData));
    new_gas_data_received = true;
    Serial.println(F("[LoRa] Dados de GasData recebidos:"));
    Serial.printf("  Nivel Gas (raw): %.0f\n", gas_data.gas_level);
    Serial.printf("  Status Chama: %.1f (%s)\n", gas_data.flame_status, gas_data.flame_status == 1.0 ? "DETECTADA" : "Nao Detectada");
}

bool lora_interpret_packet(int packetSize){
    if (packetSize == 0) return false;

    if (packetSize == sizeof(JardimData)) {
        Serial.println(F("[LoRa] Pacote JardimData detectado."));
        read_jardim_data();
        return true;
    }
    else if (packetSize == sizeof(GasData)) {
        Serial.println(F("[LoRa] Pacote GasData detectado."));
        read_gas_flame_data();
        return true; 
    }
    else {
        String msg = "";
        while (LoRa.available()) {
            msg += (char)LoRa.read();
        }
        msg.trim();
        Serial.print(F("[LoRa] Mensagem/Pacote desconhecido recebido: '"));
        Serial.print(msg);
        Serial.printf("' Tamanho: %d\n", packetSize);
        
        if (msg == "PING_CENTRAL") {
            Serial.println(F("[LoRa] Recebido PING_CENTRAL -> Respondendo ACK_CENTRAL"));
            LoRa.beginPacket();
            LoRa.print("ACK_CENTRAL");
            LoRa.endPacket();
            Serial.println(F("[LoRa] ACK_CENTRAL enviado."));
        }
        return false; // PING ou desconhecido não são dados para display principal
    }
}

void update_oled_display() {
    display.clearDisplay();
    display.setTextSize(1); // Usar tamanho 1 para caber mais informação

    // Título
    display.setCursor(0, OLED_LINE_TITLE);
    display.setTextSize(2); // Título maior
    display.print(F("RECEPTOR"));
    display.setTextSize(1); // Volta para tamanho 1

    // Dados de Umidade
    char buf[25]; // Buffer maior para S1 e S2 na mesma linha
    if(jardim_data.umidade[0] == -2.0 && (NUM_HUMIDITY_SENSORS < 2 || jardim_data.umidade[1] == -2.0)){ // -2.0 é o valor inicial
        snprintf(buf, sizeof(buf), "Umid: Aguardando...");
    } else {
        String s1_str = (jardim_data.umidade[0] == -2.0 || jardim_data.umidade[0] == -1.0) ? "N/A" : String(jardim_data.umidade[0], 1) + "%";
        String s2_str = (NUM_HUMIDITY_SENSORS < 2 || jardim_data.umidade[1] == -2.0 || jardim_data.umidade[1] == -1.0) ? "N/A" : String(jardim_data.umidade[1], 1) + "%";
        if (NUM_HUMIDITY_SENSORS > 1) {
            snprintf(buf, sizeof(buf), "U1:%s U2:%s", s1_str.c_str(), s2_str.c_str());
        } else {
            snprintf(buf, sizeof(buf), "U1:%s", s1_str.c_str());
        }
    }
    display.setCursor(0, OLED_LINE_UMID1); // Ajustado para uma linha
    display.print(buf);

    // Dados de Gás e Chama
    if(gas_data.gas_level == -2.0 && gas_data.flame_status == -2.0){ // -2.0 é o valor inicial
         snprintf(buf, sizeof(buf), "Gas/Chama: Aguardando...");
    } else {
        String gas_str = (gas_data.gas_level == -2.0 || gas_data.gas_level == -1.0) ? "N/A" : String(gas_data.gas_level, 0);
        String flame_str = (gas_data.flame_status == -2.0 || gas_data.flame_status == -1.0) ? "N/A" : (gas_data.flame_status == 1.0 ? "DETEC!" : "Nao");
        snprintf(buf, sizeof(buf), "Gas:%s Chama:%s", gas_str.c_str(), flame_str.c_str());
    }
    display.setCursor(0, OLED_LINE_GAS); // Ajustado para uma linha
    display.print(buf);
    
    // IP Address
    display.setCursor(0, OLED_LINE_IP);
    display.print(F("IP: "));
    if(WiFi.isConnected()){
        display.print(WiFi.localIP());
    } else {
        display.print(F("N/C")); 
    }
    display.display();
}

void setup() {
    Serial.begin(DEBUG_SERIAL_BAUDRATE);
    Serial.println(F("\n[Receptor] Iniciando Setup..."));
    Wire.begin(OLED_SDA_PIN, OLED_SCL_PIN);

    // Inicializa dados com valores indicando "não recebido ainda"
    for (int i = 0; i < NUM_HUMIDITY_SENSORS; i++) {
        jardim_data.umidade[i] = -2.0; // Usar -2.0 como valor inicial distinto de -1.0 (erro do emissor)
    }
    gas_data.gas_level = -2.0;
    gas_data.flame_status = -2.0;

    if (!display_init()) {
        Serial.println(F("Display OLED FALHOU! Loop infinito."));
        while(1);
    }
    default_display_message("Iniciando..."); // Mensagem inicial

    if(!LittleFS.begin()){
        Serial.println(F("Erro ao montar LittleFS."));
        default_display_message("Erro LittleFS!");
        // Considerar parar se LittleFS for crítico
    }

    wifi_connect(); 
    server_init();  

    if (!lora_chip_init()) {
        default_display_message("Erro LoRa!");
        Serial.println(F("Chip LoRa FALHOU! Loop infinito."));
        while(1);
    }
    
    default_display_message("Aguardando dados..."); // Mensagem após tudo inicializado
    Serial.println(F("[Receptor] Setup completo."));
}

void loop() {
    int packetSize = LoRa.parsePacket(); 
    if (packetSize > 0) { 
        Serial.printf("[LoRa] Pacote recebido. Tamanho: %d, RSSI: %d\n", packetSize, LoRa.packetRssi());
        lora_interpret_packet(packetSize); // Interpreta e atualiza flags new_..._data_received
        // A flag new_..._data_received não está sendo usada para forçar update,
        // o display será atualizado de qualquer forma abaixo.
        // Se quiser atualizar apenas quando há novos dados, adicione lógica com as flags.
    }
    
    update_oled_display(); // Atualiza o display com os últimos dados conhecidos

    // Pequeno delay para não sobrecarregar o processador, mas permitir resposta rápida
    delay(100); 
}
