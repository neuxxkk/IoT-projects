#include <WiFi.h>
#include <Wire.h>
#include <LoRa.h>
#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

#define SENSOR 13  // Pino do sensor de umidade

/* Definições do OLED */
#define OLED_SDA_PIN    4
#define OLED_SCL_PIN    15
#define SCREEN_WIDTH    128 
#define SCREEN_HEIGHT   64  
#define OLED_ADDR       0x3C 
#define OLED_RESET      16

/* Offset de linhas no display OLED */
#define OLED_LINE1      0
#define OLED_LINE2      SCREEN_HEIGHT/2 + 20
#define OLED_LINE3      OLED_LINE2 + 20

/* Definições para comunicação com rádio LoRa */
#define SCK_LORA        5
#define MISO_LORA       19
#define MOSI_LORA       27
#define RESET_PIN_LORA  14
#define SS_PIN_LORA     18

// Parâmetros LoRa
#define HIGH_GAIN_LORA  14    // 0 ~ 20dBm
#define BAND            915E6 // 915MHz

// Valores da calibragem
#define valor_seco 2700
#define valor_umido 1400

/* objeto do display */
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

// Protótipos
void display_init();
void escreve_display(float umid);
void escreve_serial(float umid);
void envia_lora(float umid);
bool init_comunicacao_lora();

void display_init() {
  if (!display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDR)) {
    Serial.println("[LoRa Receiver] Falha ao inicializar OLED");
  } else {
    Serial.println("[LoRa Receiver] OLED inicializado");
    display.clearDisplay();
    display.setTextSize(2);
    display.setTextColor(WHITE);
  }
}

void escreve_serial(float umid) {
  char mensagem[100];

  // limpa tela Serial
  for (int i = 0; i < 80; i++) {
    Serial.println();
  }

  // imprime umidade atual
  snprintf(mensagem, sizeof(mensagem), "- Umidade atual: %.2f%%", umid);
  Serial.println(mensagem);
}

void escreve_display(float umid) {
  char buf[16];
  snprintf(buf, sizeof(buf), "%.2f%%", umid);

  // apaga apenas a segunda linha (10px de altura)
  display.fillRect(0, OLED_LINE2, SCREEN_WIDTH, 10, BLACK);

  display.setTextSize(1);
  display.setCursor(0, OLED_LINE2);
  display.print("Umidade: ");
  display.print(buf);
  display.display();
}

void envia_lora(float umid) {
  LoRa.beginPacket();
  LoRa.write((uint8_t*)&umid, sizeof(umid));
  LoRa.endPacket();
}

bool init_comunicacao_lora() {
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

void setup() {
  Serial.begin(115200);
  pinMode(SENSOR, INPUT);

  // 1) Inicializa I2C nos pinos definidos
  Wire.begin(OLED_SDA_PIN, OLED_SCL_PIN);

  // 2) Inicializa o OLED
  display_init();

  // Mensagem inicial
  display.clearDisplay();
  display.setCursor(SCREEN_WIDTH, OLED_LINE1);
  display.print("EMISSOR");
  display.setTextSize(1);
  display.setCursor(0, OLED_LINE2);
  display.print("Ativo. Aguarde...");
  display.display();

  // 3) Inicializa LoRa (bloqueante até sucesso)
  while (!init_comunicacao_lora()) {
    delay(1000);
  }

  display.fillRect(0, OLED_LINE2, SCREEN_WIDTH, 30, BLACK);
}

void loop() {
  // 4) Leitura e escala de 0–100%
  int raw = analogRead(SENSOR);
  
  // Mapeia a leitura para uma escala de 0% (seco) a 100% (úmido)
  int umidadePercentual = map(raw, valor_seco, valor_umido, 0, 100);
  // Garante que o valor esteja entre 0% e 100%
  float umid = constrain(umidadePercentual, 0, 100);

  // 5) Se algo der errado na leitura
  if (isnan(umid)) {
    Serial.println("Erro ao ler sensor de umidade!");
  } else {
    escreve_serial(umid);
    escreve_display(umid);
    envia_lora(umid);
  }

  delay(1500);
}