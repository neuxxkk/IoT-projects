// Web librarys
#include <WiFi.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <LittleFS.h>

// Instanciação do objeto da classe AsyncWebServer
AsyncWebServer server(80);

// LoRa
#include <LoRa.h>
#include <SPI.h>

// OLED LoRa
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

/* Definicoes para comunicação com radio LoRa */
#define SCK_LORA           5
#define MISO_LORA          19
#define MOSI_LORA          27
#define RESET_PIN_LORA     14
#define SS_PIN_LORA        18

#define HIGH_GAIN_LORA     20  /* dBm */
#define BAND               915E6  /* 915MHz de frequencia */

/* Definicoes do OLED */
#define OLED_SDA_PIN    4
#define OLED_SCL_PIN    15
#define SCREEN_WIDTH    128 
#define SCREEN_HEIGHT   64  
#define OLED_ADDR       0x3C 
#define OLED_RESET      16

/* Offset de linhas no display OLED */
#define OLED_LINE1      0
#define OLED_LINE2      OLED_LINE1 + 40
#define OLED_LINE3      OLED_LINE2 + 10

/* Definicoes gerais */
#define DEBUG_SERIAL_BAUDRATE    115200

/* Variaveis e objetos globais */
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

// Others
int rssi;
float umidade = 0.00;

// WIFI
const char* ssid     = "JV";
const char* password = "galoDoido";

// Funcoes
void server_init(void);
void conect_wifi(void);
void display_init(void);
void escreve_display(float);
bool init_comunicacao_lora(void);
void get_lora_data(void);

void envia_web(float);

/*
    TRY begin Server
*/

void server_init(){
    // Servidor começa à ouvir os clientes
    server.begin();
      // disponibiliza o url "/" com o conteúdo da String abaixo
    server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
        request->send(LittleFS, "/index.html", "text/html");
    });
}

/*
    TRY connect to WIFI
*/
void conect_wifi(){
    WiFi.begin(ssid, password);
    Serial.println(WiFi.localIP());
}

/* 
    Funcao: inicializa comunicacao com o display OLED
*/ 
void display_init(void) {
    if(!display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDR)) 
    {
        Serial.println("[LoRa Receiver] Falha ao inicializar comunicacao com OLED");        
    }
    else
    {
        Serial.println("[LoRa Receiver] Comunicacao com OLED inicializada com sucesso");
    
        /* Limpa display e configura tamanho de fonte */
        display.clearDisplay();
        display.setTextSize(2);
        display.setTextColor(WHITE);
    }
}

/*
    Funcao que printa no OLED
*/
void escreve_display(float umid) {
    char buf[16];
    snprintf(buf, sizeof(buf), "%.2f%%", umid);

    // apaga apenas a segunda linha (10px de altura)
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

/*
    Funcao: inicia comunicação com chip LoRa
*/
bool init_comunicacao_lora(void) {
    bool status_init = false;
    Serial.println("[LoRa Receiver] Tentando iniciar comunicacao com o radio LoRa...");
    SPI.begin(SCK_LORA, MISO_LORA, MOSI_LORA, SS_PIN_LORA);
    LoRa.setPins(SS_PIN_LORA, RESET_PIN_LORA, LORA_DEFAULT_DIO0_PIN);
    
    if (!LoRa.begin(BAND)) {
        Serial.println("[LoRa Receiver] Comunicacao com o radio LoRa falhou. Nova tentativa em 1 segundo...");        
        delay(1000);
        status_init = false;
    }else {
        /* Configura o ganho do receptor LoRa para 20dBm, o maior ganho possível (visando maior alcance possível) */ 
        LoRa.setTxPower(HIGH_GAIN_LORA); 
        Serial.println("[LoRa Receiver] Comunicacao com o radio LoRa ok");
        status_init = true;
    }

    return status_init;
}

/*
    FUNCAO envia dados para web
*/

void envia_web(float umidade) {
    //
}

/* 
    Funcao de setup 
*/
void setup() {
    Serial.begin(DEBUG_SERIAL_BAUDRATE);
    while (!Serial);

        // Beggining LittleFS
    if(!LittleFS.begin()){
        Serial.println("An Error has occurred while mounting LittleFS");
        return;
    }

    // Wifi connection
    conect_wifi();
    server_init();

    // ** Nova rota que retorna o valor da umidade em texto puro **
    server.on("/umidade", HTTP_GET, [](AsyncWebServerRequest *request) {
        char buf[16];
        dtostrf(umidade, 6, 2, buf);
        request->send(200, "text/plain", buf);
    });

    // Configuracao display OLED
    Wire.begin(OLED_SDA_PIN, OLED_SCL_PIN);
    display_init();
    // Mensagem inicial
    display.clearDisplay();
    display.setCursor(SCREEN_WIDTH, OLED_LINE1);
    display.print("RECEPTOR");
    display.setTextSize(1);
    display.setCursor(0, OLED_LINE2);
    display.print("Ativo. Aguarde...");
    display.display();

    /* Tenta, até obter sucesso, comunicacao com o chip LoRa */
    while(init_comunicacao_lora() == false);  
    display.fillRect(0, OLED_LINE2, SCREEN_WIDTH, 30, BLACK); // fim do aguarde
}

/* Programa principal */
void loop() {
    /* Verifica se chegou alguma informação (do tamanho esperado, STRUCT - more data-) */
    if (LoRa.parsePacket()) {
        float umidadeRecebida;
        LoRa.readBytes((uint8_t*)&umidadeRecebida, sizeof(umidadeRecebida));
        umidade = umidadeRecebida;

        escreve_display(umidade);
        Serial.print("\nUmidade do solo: ");
        Serial.print(umidade);
        Serial.print("%");
    }
}
