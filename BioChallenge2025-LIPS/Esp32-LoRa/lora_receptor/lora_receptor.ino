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
#define HIGH_GAIN_LORA  14    // 0 ~ 20dBm
#define BAND            915E6 // 915MHz

// Definicoes do OLED 
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

// Struct para dados do LoRa
struct LoraData {
  float umidade;
}data;

// Identificadores
#define DEBUG_SERIAL_BAUDRATE    115200

// WIFI
const char* ssid     = "vitornms";
const char* password = "qwerasdf";
// Parâmetros de rede para ter IP estático
IPAddress local_IP(192,168,172,65);    // 172 é o IP do ponto de acesso
IPAddress gateway(192,168,172,1);
IPAddress subnet(255,255,255,0);

// Server declaration
AsyncWebServer server(80);

// Outros
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);


// Funções de inicialização
bool display_init(void);
bool lora_chip_init(void);
void server_init(void);

// Funções de primeiro contato
bool wifi_connect(void);
void default_display(void);

// Funções de leitura
void component_read(void);
bool lora_interpret(void);

// Funções de escrita e envio
void display_write(void);


/*
    Funcoes de inicialização
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
  } else {
    Serial.println("[LoRa Receiver] OLED inicializado");
    display.clearDisplay();
    display.setTextSize(2);
    display.setTextColor(WHITE);
    default_display();
    return true;
  }
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
    Begin Server
    - Inicia o servidor web
    - Cria ponto de acesso
*/
void server_init(){
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
    Funções de primeiro contato
*/
/*
    Connect to WIFI
    - Tenta conectar ao wifi
*/
bool wifi_connect(){
  WiFi.mode(WIFI_STA);
  if (!WiFi.config(local_IP, gateway, subnet)) {
    Serial.println("Falha ao configurar IP estático"); 
  }
  WiFi.begin(ssid, password);
  Serial.println("Tentando conectar ao WiFi...");
  while (WiFi.status() != WL_CONNECTED) {
    delay(1000);
    Serial.print('.');
  }
  Serial.printf("\nConectado ao wifi %s com IP: ", ssid);
  Serial.println(WiFi.localIP());  
  return true;
}

/*
    Função que printa deafult no OLED
*/
void default_display() {
    display.clearDisplay();
    display.setCursor(SCREEN_WIDTH, OLED_LINE1);
    display.print("RECEPTOR");
    display.setTextSize(1);
    display.setCursor(0, OLED_LINE2);
    display.print("Esperando componente. Aguarde...");
    display.display();
}

/*
    Funcoes de leitura
*/
/*
    Função que le radios LoRa
    - Verifica se chegou alguma informação (do tamanho esperado, STRUCT - more data-)
*/
void component_read() {
    LoRa.readBytes((uint8_t*)&data, sizeof(data));
}


/*
    Função que interpreta os dados recebidos
    - Verifica se chegou algum pacote
    - Se o pacote for do tamanho esperado, lê os dados
    - Se não, verifica se é um PING_CENTRAL e responde com ACK_CENTRAL
*/
bool lora_interpret(){
    int packetSize = LoRa.parsePacket();
    if (packetSize) {
        if (packetSize == sizeof(LoraData)){
            component_read();
            Serial.printf("\n[LoRa] Recebido LoraData -> Posting on IP: \n");
            Serial.println(WiFi.localIP());
        }else {
            String msg = LoRa.readString();
            msg.trim();
            
            // 1) Se for handshake de descoberta, responde ACK
            if (msg == "PING_CENTRAL") {
                Serial.println("[LoRa] Recebido PING_CENTRAL -> Respondendo ACK_CENTRAL");
                LoRa.beginPacket();
                LoRa.print("ACK_CENTRAL");
                LoRa.endPacket();
            }
        }
        return true;
    }
    return false;
}

/*
    Funcao que printa no OLED
*/
void display_write() {
    char buf[16];
    snprintf(buf, sizeof(buf), "%.2f%%", data.umidade);

    display.fillRect(0, OLED_LINE2, SCREEN_WIDTH, 20, BLACK);

    display.setTextSize(1);
    display.setCursor(0, OLED_LINE2);
    display.print("Umidade: ");
    display.print(buf);
    display.setCursor(0, OLED_LINE3);
    display.print("IP: ");
    display.print(WiFi.localIP());
    display.display();
}

void setup() {
    Serial.begin(DEBUG_SERIAL_BAUDRATE);

    Wire.begin(OLED_SDA_PIN, OLED_SCL_PIN);

    display_init();

    if(!LittleFS.begin()){
        Serial.println("An Error has occurred while mounting LittleFS");
        return;
    }

    wifi_connect();
    server_init();

    while(lora_chip_init() == false);
}

void loop() {
    while (!lora_interpret());
    display_write();
    delay(10);
}
