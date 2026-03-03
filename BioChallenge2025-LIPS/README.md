# 🌿 BioChallenge 2025 — LIPS/UFMG

Conjunto de nós sensores e receptor central desenvolvidos para o **BioChallenge 2025** do laboratório **LIPS (UFMG)**. O sistema monitora condições ambientais de uma estufa/jardim e detecta riscos de incêndio, transmitindo os dados via **LoRa (915 MHz)** e disponibilizando-os em uma interface web.

---

## 📂 Sub-projetos

| Pasta | Dispositivo | Função |
|-------|------------|--------|
| [`Jardim/`](./Jardim) | ESP32 | Lê umidade do solo, aciona irrigação e envia dados via LoRa |
| [`Central/`](./Central) | ESP32 | Recebe dados de todos os nós via LoRa e os serve em interface web Wi-Fi |
| [`Fogo/`](./Fogo) | ESP32 | Detecta gás (MQ-2) e chamas; envia alertas via LoRa |

---

## 🏗️ Arquitetura do Sistema

```
[Jardim ESP32] ──LoRa──┐
[Fogo ESP32]   ──LoRa──┴──► [Central ESP32] ──Wi-Fi──► Navegador (Dashboard)
```

Cada nó sensor verifica periodicamente a existência de uma central via **handshake LoRa** (`PING_CENTRAL` / `ACK_CENTRAL`). Se nenhuma central for encontrada, o próprio nó assume o papel de central.

---

## 🌱 Jardim — Nó de Umidade

**Hardware:** ESP32 · 2× sensor de umidade capacitivo · 2× motor (irrigação) · OLED SSD1306 · módulo LoRa

**Funcionalidades:**
- Leitura de 2 sensores de umidade do solo
- Acionamento automático de motores de irrigação quando a umidade cai abaixo do limiar
- Exibição de dados no display OLED
- Sincronização de hora via NTP e deep sleep entre leituras para economia de energia
- Modo automático: **central** (sem central LoRa detectada) ou **componente** (envia dados via LoRa)

**Bibliotecas necessárias:**
- `AsyncTCP`, `ESPAsyncWebServer`
- `LoRa` (Sandeep Mistry)
- `Adafruit GFX`, `Adafruit SSD1306`
- `LittleFS`

---

## 📡 Central — Receptor e Servidor Web

**Hardware:** ESP32 · OLED SSD1306 · módulo LoRa

**Funcionalidades:**
- Recebe pacotes LoRa dos nós Jardim e Fogo
- Conecta à rede Wi-Fi e serve dashboard HTML via `LittleFS`
- Endpoint `/umidade` retorna leituras em JSON
- Exibe IP e dados dos sensores no display OLED

---

## 🔥 Fogo — Nó de Detecção de Gás e Chamas

**Hardware:** ESP32 · sensor de gás MQ-2 (pino 36) · sensor de chama digital (pino 2) · OLED SSD1306 · módulo LoRa

**Funcionalidades:**
- Monitoramento contínuo de concentração de gás e presença de chamas
- Transmissão de alertas via LoRa para a central
- Deep sleep entre leituras com wake-up por timer (NTP)

---

## 🔌 Pinagem Comum (ESP32)

| Sinal | Pino |
|-------|------|
| LoRa SCK | 5 |
| LoRa MISO | 19 |
| LoRa MOSI | 27 |
| LoRa RST | 14 |
| LoRa SS | 18 |
| OLED SDA | 4 |
| OLED SCL | 15 |
| OLED RST | 16 |

---

## 🚀 Como Usar

1. Instale as bibliotecas listadas acima no **Arduino IDE** ou **PlatformIO**.
2. Ajuste as credenciais Wi-Fi (`ssid` / `password`) e o IP estático em cada `.ino`.
3. Faça upload do firmware correspondente em cada ESP32.
4. Ligue primeiro a **Central** e depois os nós sensores.
5. Acesse o IP da central no navegador para visualizar o dashboard.
