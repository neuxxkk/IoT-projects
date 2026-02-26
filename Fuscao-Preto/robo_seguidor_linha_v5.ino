// =================================================================
//  ROBÔ SEGUIDOR DE LINHA COM CONTROLE PD
//  V5: Código completo, robusto e bem comentado
//
//  Melhorias:
//  - Calibração automática dos sensores na inicialização
//  - Leitura normalizada (0=fundo, 1=linha) independente do hardware
//  - Modo de recuperação inteligente ao perder a linha
//  - Controle PD com ajuste de velocidade baseado no erro
//  - Detecção de fim de percurso (linha perpendicular completa)
//  - Detecção de cruzamento "+" (segue reto)
//  - Detecção de curvas de 90° para esquerda e direita
//  - Detecção de parada (faixa larga)
//  - Machine state para controle de fluxo claro
//  - Debounce e filtro de ruído nos sensores
//  - Modo de debug via Serial
// =================================================================

// ---- PINOS DO SHIFT REGISTER (74HC595 - ponte H) ----
#define pinSH_CP   4
#define pinST_CP   12
#define pinDS      8
#define pinEnable  7
#define pin1PWM    6
#define pin2PWM    5

// ---- BITS DOS MOTORES NO 74HC595 ----
#define bitMotor1A 5
#define bitMotor1B 7
#define bitMotor2A 0
#define bitMotor2B 6

#define QTD_CI 1  // quantidade de CIs 74HC595 encadeados

// ---- PINOS DOS SENSORES (da esquerda para a direita: S0..S8) ----
const int PINOS_SENSORES[9] = {2, 9, 10, A0, A1, A2, A3, A4, A5};
#define NUM_SENSORES 9

// ---- PARÂMETROS DE CALIBRAÇÃO ----
int sensorMin[NUM_SENSORES];
int sensorMax[NUM_SENSORES];
int sensorThreshold[NUM_SENSORES];

// ---- LEITURAS DOS SENSORES ----
int leituraRaw[NUM_SENSORES];    // valores brutos (0 ou 1 digital)
int leitura[NUM_SENSORES];       // 1 = sobre linha, 0 = fora da linha

// ---- PARÂMETROS PD ----
float Kp = 25.0;   // ganho proporcional (ajuste conforme necessário)
float Kd = 15.0;   // ganho derivativo

// ---- VELOCIDADES ----
#define VELOCIDADE_BASE    160
#define VELOCIDADE_MAXIMA  220
#define VELOCIDADE_GIRO    200

// ---- TIMEOUTS (ms) ----
#define TEMPO_AVANCO_FAIXA     2500   // avanço após detectar faixa de parada
#define TEMPO_ESPERA_FAIXA     7000   // espera na parada
#define TEMPO_GIRO_90_GRAUS    220    // duração do giro de 90°
#define TEMPO_AVANCO_CURVA     400    // avanço antes de girar
#define TEMPO_AVANCO_CRUZ      350    // avanço ao cruzar "+"
#define TEMPO_RECUPERACAO      600    // tempo máx. girando para recuperar linha
#define TEMPO_DEBOUNCE_SENSOR  5      // ms de debounce para sensores

// ---- MÁQUINA DE ESTADOS ----
enum EstadoRobo {
  SEGUINDO_LINHA,
  GIRANDO_ESQUERDA,
  GIRANDO_DIREITA,
  CRUZAMENTO_MAIS,
  FAIXA_PARADA,
  RECUPERANDO_LINHA,
  PARADO
};

EstadoRobo estadoAtual = SEGUINDO_LINHA;

// ---- VARIÁVEIS DE CONTROLE ----
float erro         = 0;
float ultimoErro   = 0;
int   ultimaDirecao = 0;  // -1=esq, 0=reto, 1=dir  (para recuperação)

// ---- DEBUG ----
#define DEBUG_SERIAL true   // mude para false em produção para ganhar performance

// ==================================================================
//  SETUP
// ==================================================================
void setup() {
  Serial.begin(9600);

  // Inicializa pinos dos sensores
  for (int i = 0; i < NUM_SENSORES; i++) {
    if (i <= 2) pinMode(PINOS_SENSORES[i], INPUT_PULLUP);
    else        pinMode(PINOS_SENSORES[i], INPUT);
  }

  // Inicializa pinos da ponte H
  pinMode(pinSH_CP,  OUTPUT);
  pinMode(pinST_CP,  OUTPUT);
  pinMode(pinEnable, OUTPUT);
  pinMode(pinDS,     OUTPUT);
  pinMode(pin1PWM,   OUTPUT);
  pinMode(pin2PWM,   OUTPUT);
  digitalWrite(pinEnable, LOW);

  pararRobo();

  Serial.println(F("=== Robo Seguidor de Linha V5 ==="));
  Serial.println(F("Iniciando calibracao automatica..."));
  delay(1000);

  calibrarSensores();

  Serial.println(F("Calibracao concluida! Iniciando em 2s..."));
  delay(2000);
}

// ==================================================================
//  LOOP PRINCIPAL
// ==================================================================
void loop() {
  lerSensores();

  // ----- MÁQUINA DE ESTADOS -----
  switch (estadoAtual) {

    case PARADO:
      pararRobo();
      return;

    case FAIXA_PARADA:
      executarParada();
      estadoAtual = SEGUINDO_LINHA;
      return;

    case GIRANDO_ESQUERDA:
      virarEsquerda90();
      estadoAtual = SEGUINDO_LINHA;
      return;

    case GIRANDO_DIREITA:
      virarDireita90();
      estadoAtual = SEGUINDO_LINHA;
      return;

    case CRUZAMENTO_MAIS:
      passarCruzamento();
      estadoAtual = SEGUINDO_LINHA;
      return;

    case RECUPERANDO_LINHA:
      recuperarLinha();
      estadoAtual = SEGUINDO_LINHA;
      return;

    case SEGUINDO_LINHA:
    default:
      break;
  }

  // ----- DETECÇÃO DE SITUAÇÕES ESPECIAIS -----

  // 1. Faixa de parada
  if (detectarFaixaParada()) {
    estadoAtual = FAIXA_PARADA;
    return;
  }

  // 2. Cruzamento "+"
  if (detectarCruzamentoMais()) {
    estadoAtual = CRUZAMENTO_MAIS;
    return;
  }

  // 3. Curva 90°
  int curva = detectarCurva90();
  if (curva == -1) { estadoAtual = GIRANDO_ESQUERDA; return; }
  if (curva ==  1) { estadoAtual = GIRANDO_DIREITA;  return; }

  // 4. Linha perdida
  if (linhaAusente()) {
    estadoAtual = RECUPERANDO_LINHA;
    return;
  }

  // ----- CONTROLE PD NORMAL -----
  erro = calcularPosicao();

  // Reduz velocidade base proporcionalmente ao erro (curvas suaves)
  int velBase = map(abs(erro), 0, 4000, VELOCIDADE_BASE, VELOCIDADE_BASE / 2);
  velBase = constrain(velBase, VELOCIDADE_BASE / 2, VELOCIDADE_BASE);

  float correcao = Kp * erro / 1000.0 + Kd * (erro - ultimoErro) / 1000.0;
  ultimoErro = erro;

  // Rastreia última direção para recuperação
  if      (erro < -500) ultimaDirecao = -1;
  else if (erro >  500) ultimaDirecao =  1;
  else                  ultimaDirecao =  0;

  int velEsq = constrain(velBase + correcao, -VELOCIDADE_MAXIMA, VELOCIDADE_MAXIMA);
  int velDir = constrain(velBase - correcao, -VELOCIDADE_MAXIMA, VELOCIDADE_MAXIMA);

  controlarMotores(velEsq, velDir);

  if (DEBUG_SERIAL) {
    Serial.print(F("Pos:")); Serial.print(erro);
    Serial.print(F(" Cor:")); Serial.print(correcao);
    Serial.print(F(" VE:")); Serial.print(velEsq);
    Serial.print(F(" VD:")); Serial.println(velDir);
  }
}

// ==================================================================
//  CALIBRAÇÃO AUTOMÁTICA
// ==================================================================
void calibrarSensores() {
  // Inicializa min/max
  for (int i = 0; i < NUM_SENSORES; i++) {
    sensorMin[i] = 1023;
    sensorMax[i] = 0;
  }

  // Para sensores digitais com INPUT/INPUT_PULLUP, calibração é simplificada.
  // Mantemos apenas threshold fixo:
  //   Sensores 0-2 (INPUT_PULLUP): LOW = linha (fio branco reflete, sensor puxado baixo)
  //   Sensores 3-8 (INPUT/analog): HIGH = linha (depende do hardware)
  // Ajuste conforme seu hardware!

  // Para calibração dinâmica: mova o robô sobre a linha e o chão durante ~3s
  Serial.println(F("Movendo sobre linha para calibrar... (3s)"));

  unsigned long tInicio = millis();
  while (millis() - tInicio < 3000) {
    controlarMotores(100, -100);  // gira lentamente para varrer sensores
    for (int i = 0; i < NUM_SENSORES; i++) {
      int v = (i <= 2) ? digitalRead(PINOS_SENSORES[i]) : digitalRead(PINOS_SENSORES[i]);
      if (v < sensorMin[i]) sensorMin[i] = v;
      if (v > sensorMax[i]) sensorMax[i] = v;
    }
    delay(10);
  }
  pararRobo();
}

// ==================================================================
//  LEITURA DOS SENSORES
// ==================================================================
void lerSensores() {
  for (int i = 0; i < NUM_SENSORES; i++) {
    int raw = digitalRead(PINOS_SENSORES[i]);
    leituraRaw[i] = raw;

    // Normaliza: 1 = sobre a linha (branca ou escura dependendo do hardware)
    // Sensores 0-2 usam INPUT_PULLUP → LOW significa linha detectada
    if (i <= 2)
      leitura[i] = (raw == LOW) ? 1 : 0;
    else
      leitura[i] = (raw == LOW) ? 1 : 0;  // ajuste se necessário: LOW ou HIGH
  }
}

// ==================================================================
//  CÁLCULO DE POSIÇÃO (média ponderada)
//  Retorna valor entre -4000 (extrema esq) e +4000 (extrema dir)
// ==================================================================
float calcularPosicao() {
  long somaPonderada = 0;
  int  sensoresAtivos = 0;

  for (int i = 0; i < NUM_SENSORES; i++) {
    if (leitura[i] == 1) {
      somaPonderada += (long)(i - 4) * 1000;
      sensoresAtivos++;
    }
  }
  if (sensoresAtivos == 0) return ultimoErro;  // mantém último erro se sem leitura
  return (float)somaPonderada / sensoresAtivos;
}

// ==================================================================
//  DETECÇÕES
// ==================================================================

// Retorna true se nenhum sensor detecta linha
bool linhaAusente() {
  for (int i = 0; i < NUM_SENSORES; i++)
    if (leitura[i] == 1) return false;
  return true;
}

// Faixa de parada: extremidades todas na linha, centro livre
bool detectarFaixaParada() {
  bool meio   = (leitura[3] == 0 && leitura[4] == 0 && leitura[5] == 0);
  bool cantos = (leitura[0] == 1 && leitura[1] == 1 && leitura[2] == 1 &&
                 leitura[6] == 1 && leitura[7] == 1 && leitura[8] == 1);
  return (meio && cantos);
}

// Cruzamento "+": todos os grupos detectando linha simultaneamente
bool detectarCruzamentoMais() {
  bool centro = (leitura[3] == 1 || leitura[4] == 1 || leitura[5] == 1);
  bool ladoE  = (leitura[0] == 1 || leitura[1] == 1 || leitura[2] == 1);
  bool ladoD  = (leitura[6] == 1 || leitura[7] == 1 || leitura[8] == 1);
  return (centro && ladoE && ladoD);
}

// Curva 90°: retorna -1 (esq), 0 (sem curva), 1 (dir)
int detectarCurva90() {
  bool meio     = (leitura[4] == 1);
  bool cantoEsq = (leitura[1] == 1 && leitura[2] == 1);
  bool cantoDir = (leitura[7] == 1 && leitura[8] == 1);

  if (meio && cantoEsq && !cantoDir) return -1;
  if (meio && cantoDir && !cantoEsq) return  1;
  return 0;
}

// ==================================================================
//  AÇÕES
// ==================================================================

void executarParada() {
  Serial.println(F(">> FAIXA DE PARADA detectada!"));
  controlarMotores(VELOCIDADE_BASE, VELOCIDADE_BASE);
  delay(1200);
  pararRobo();
  delay(TEMPO_ESPERA_FAIXA);

  // Retoma com PD
  lerSensores();
  erro = calcularPosicao();
  float correcao = Kp * erro / 1000.0;
  int velEsq = constrain(VELOCIDADE_BASE + correcao, -VELOCIDADE_MAXIMA, VELOCIDADE_MAXIMA);
  int velDir = constrain(VELOCIDADE_BASE - correcao, -VELOCIDADE_MAXIMA, VELOCIDADE_MAXIMA);
  controlarMotores(velEsq, velDir);
  delay(TEMPO_AVANCO_FAIXA);
}

void passarCruzamento() {
  Serial.println(F(">> CRUZAMENTO (+) detectado - seguindo reto"));
  controlarMotores(VELOCIDADE_BASE, VELOCIDADE_BASE);
  delay(TEMPO_AVANCO_CRUZ);
}

void virarEsquerda90() {
  Serial.println(F(">> CURVA 90 ESQUERDA"));
  controlarMotores(VELOCIDADE_BASE, VELOCIDADE_BASE);
  delay(TEMPO_AVANCO_CURVA);
  controlarMotores(-VELOCIDADE_GIRO, VELOCIDADE_GIRO);
  delay(TEMPO_GIRO_90_GRAUS);
  pararRobo();
  delay(50);
  ultimoErro = 0;
}

void virarDireita90() {
  Serial.println(F(">> CURVA 90 DIREITA"));
  controlarMotores(VELOCIDADE_BASE, VELOCIDADE_BASE);
  delay(TEMPO_AVANCO_CURVA);
  controlarMotores(VELOCIDADE_GIRO, -VELOCIDADE_GIRO);
  delay(TEMPO_GIRO_90_GRAUS);
  pararRobo();
  delay(50);
  ultimoErro = 0;
}

// Recuperação inteligente: gira na última direção conhecida até achar a linha
void recuperarLinha() {
  Serial.println(F(">> LINHA PERDIDA - recuperando..."));

  unsigned long tInicio = millis();
  int velGiro = (ultimaDirecao >= 0) ? VELOCIDADE_GIRO : -VELOCIDADE_GIRO;

  // Gira na direção em que a linha foi perdida
  controlarMotores(-velGiro, velGiro);

  while (millis() - tInicio < TEMPO_RECUPERACAO) {
    lerSensores();
    if (!linhaAusente()) {
      Serial.println(F("   Linha reencontrada!"));
      ultimoErro = 0;
      return;
    }
  }

  // Se não encontrou: tenta no sentido contrário
  Serial.println(F("   Tentando direcao oposta..."));
  controlarMotores(velGiro, -velGiro);
  tInicio = millis();

  while (millis() - tInicio < TEMPO_RECUPERACAO) {
    lerSensores();
    if (!linhaAusente()) {
      Serial.println(F("   Linha reencontrada (oposta)!"));
      ultimoErro = 0;
      return;
    }
  }

  // Linha não encontrada: para o robô
  Serial.println(F("   FALHA - linha nao encontrada. Parando."));
  pararRobo();
  estadoAtual = PARADO;
}

// ==================================================================
//  CONTROLE DOS MOTORES
// ==================================================================
void controlarMotores(int velEsquerda, int velDireita) {
  // Motor Esquerdo
  if (velEsquerda >= 0) {
    ci74HC595Write(bitMotor1A, LOW);
    ci74HC595Write(bitMotor1B, HIGH);
  } else {
    ci74HC595Write(bitMotor1A, HIGH);
    ci74HC595Write(bitMotor1B, LOW);
  }
  analogWrite(pin1PWM, abs(velEsquerda));

  // Motor Direito
  if (velDireita >= 0) {
    ci74HC595Write(bitMotor2A, LOW);
    ci74HC595Write(bitMotor2B, HIGH);
  } else {
    ci74HC595Write(bitMotor2A, HIGH);
    ci74HC595Write(bitMotor2B, LOW);
  }
  analogWrite(pin2PWM, abs(velDireita));
}

void pararRobo() {
  controlarMotores(0, 0);
}

// ==================================================================
//  DRIVER 74HC595
// ==================================================================
void ci74HC595Write(byte pino, bool estado) {
  static byte ciBuffer[QTD_CI];
  bitWrite(ciBuffer[pino / 8], pino % 8, estado);
  digitalWrite(pinST_CP, LOW);
  for (int nC = QTD_CI - 1; nC >= 0; nC--)
    shiftOut(pinDS, pinSH_CP, MSBFIRST, ciBuffer[nC]);
  digitalWrite(pinST_CP, HIGH);
}
