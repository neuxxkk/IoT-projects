# 🤖 Fuscão Preto — Robô Seguidor de Linha

Robô autônomo seguidor de linha desenvolvido com **Arduino UNO**, controle **PD** e 9 sensores infravermelhos. O projeto foi criado como atividade prática de sistemas embarcados e controle de trajetória.

---

## 🧠 Funcionamento

O robô lê continuamente 9 sensores IR posicionados em linha, calcula o **erro de posição** via média ponderada e aplica um controlador **PD (Proporcional-Derivativo)** para ajustar as velocidades dos motores, mantendo o robô sobre a linha.

### Estados da Máquina de Estados

| Estado | Descrição |
|--------|-----------|
| `SEGUINDO_LINHA` | Controle PD normal |
| `GIRANDO_ESQUERDA` | Curva de 90° para a esquerda |
| `GIRANDO_DIREITA` | Curva de 90° para a direita |
| `CRUZAMENTO_MAIS` | Cruzamento em "+" — segue reto |
| `FAIXA_PARADA` | Faixa perpendicular — para e aguarda |
| `RECUPERANDO_LINHA` | Linha perdida — busca na última direção |
| `PARADO` | Robô parado após falha de recuperação |

---

## 🛠️ Hardware

| Componente | Detalhe |
|-----------|---------|
| **Microcontrolador** | Arduino UNO |
| **Sensores** | 9× sensor IR digital (pinos `2, 9, 10, A0–A5`) |
| **Driver de motores** | 74HC595 (shift register) + PWM |
| **Motores** | 2× DC com controle de velocidade independente |

---

## 📐 Parâmetros de Controle (ajustáveis no código)

```cpp
float Kp = 25.0;             // Ganho proporcional
float Kd = 15.0;             // Ganho derivativo
#define VELOCIDADE_BASE  160  // Velocidade normal (0–255)
#define VELOCIDADE_MAXIMA 220 // Velocidade máxima
```

---

## 🔌 Pinagem

| Sinal | Pino Arduino |
|-------|-------------|
| Shift Register (SH_CP) | 4 |
| Shift Register (ST_CP) | 12 |
| Shift Register (DS) | 8 |
| Enable ponte H | 7 |
| PWM Motor 1 | 6 |
| PWM Motor 2 | 5 |
| Sensores S0–S2 | 2, 9, 10 (INPUT_PULLUP) |
| Sensores S3–S8 | A0, A1, A2, A3, A4, A5 |

---

## 🚀 Como Usar

1. Abra `robo_seguidor_linha_v5.ino` no **Arduino IDE**.
2. Selecione a placa **Arduino UNO** e a porta COM correta.
3. Faça o upload.
4. Na inicialização, o robô executa **calibração automática** durante 3 segundos — posicione-o sobre a linha nesse momento.
5. Após a calibração, o robô inicia o percurso automaticamente.

> 💡 Ative `DEBUG_SERIAL true` no código para monitorar os valores pelo Serial Monitor (9600 baud).
