// Robo irrigador - FAZ Makerspace
// Arduino Mega 2560 + TFT 3.5" ILI9488 (SPI) + 2 sensores capacitivos de umidade
// + HC-SR04 (nivel do reservatorio) + modulo rele 5V + mini bomba 3-6V + LEDs + buzzer.
//
// Biblioteca: "GFX Library for Arduino" (moononournation).
// Monitor Serial a 115200 baud, fim de linha "Nova linha".
//
// IMPORTANTE: tudo que precisa ser descoberto na bancada se ajusta por comando
// e grava na EEPROM com 'g'. Nao precisa regravar o programa para calibrar.
//
// COMANDOS
//   r = leituras agora          p = mostra a configuracao
//   b = testa a bomba 3 s       a = automatico liga/desliga
//   l = testa os LEDs           z = testa o buzzer
//   1 = sensores NO AR    -> grava como SECO
//   2 = sensores NA AGUA  -> grava como MOLHADO
//   n = usar nivel (ultrassom) liga/desliga
//   v = usa a distancia atual como "reservatorio vazio"
//   i = inverte a logica do rele
//   y = buzzer ativo / passivo
//   d = tela liga/desliga       t = redesenha a tela
//   g = GRAVA a configuracao na EEPROM (mantem apos desligar)
//   x = volta aos valores de fabrica (nao grava; use 'g' depois)
//   ? = ajuda

#include <Arduino_GFX_Library.h>
#include <EEPROM.h>

// ---------------- PINOS (fixos, ver o guia de bancada) ----------------
const uint8_t PINO_UMID1   = A0;
const uint8_t PINO_UMID2   = A1;
const uint8_t PINO_TRIG    = 22;
const uint8_t PINO_ECHO    = 23;
const uint8_t PINO_RELE    = 24;
const uint8_t PINO_LED_VD  = 26;    // verde: solo ok
const uint8_t PINO_LED_VM  = 27;    // vermelho: solo seco / reservatorio vazio
const uint8_t PINO_LED_AZ  = 28;    // azul: regando
const uint8_t PINO_BUZZER  = 30;
const int8_t  TFT_CS       = 53;    // SPI do Mega: MOSI 51, SCK 52 (MISO 50 nao usado)
const int8_t  TFT_DC       = 49;
const int8_t  TFT_RST      = 48;

// ---------------- REGRAS DA REGA (fixas) ----------------
const int LIGA_ABAIXO_DE    = 35;            // % de umidade media: abaixo disso rega
const int DESLIGA_ACIMA_DE  = 60;            // % de umidade media: acima disso para
const unsigned long BOMBA_MAX_MS   = 8000;   // tempo maximo por ciclo de rega
const unsigned long ESPERA_MIN_MS  = 15000;  // pausa entre regas (agua infiltrar)

// ---------------- VALORES DE FABRICA (ponto de partida) ----------------
const int  FAB_SECO = 520, FAB_MOLHADO = 260;
const int  FAB_RESERV_VAZIO_CM = 15;
const bool FAB_RELE_ATIVO_BAIXO = true;
const bool FAB_USAR_NIVEL       = true;
const bool FAB_BUZZER_PASSIVO   = false;     // buzzer ativo (so liga o pino)
const bool FAB_BUZZER_ATIVO_BAIXO = false;   // true = modulo que apita com nivel BAIXO
const bool FAB_USAR_TELA        = true;
// Velocidade do SPI da tela. Divisor com resistor alto obriga a baixar:
// a impedancia do divisor + capacitancia dos fios arredonda o sinal.
const int32_t TELA_VEL[] = {1000000L, 2000000L, 4000000L, 8000000L};
const uint8_t TELA_VEL_N = 4;
const uint8_t FAB_TELA_VEL_IDX = 1;          // 2 MHz: seguro com divisor 1k/2k

// ------------------------------------------------------------------
struct Config {
  uint16_t magia;
  int  seco1, molhado1, seco2, molhado2;
  int  reservVazioCm;
  bool releAtivoBaixo;
  bool usarNivel;
  bool buzzerPassivo;
  bool buzzerAtivoBaixo;
  bool usarTela;
  uint8_t telaVelIdx;
};
// MAGIA muda sempre que a struct muda, para nao ler lixo de uma versao antiga
const uint16_t MAGIA = 0xFA87;      // FAZ / OS-0885
const int ENDERECO_CFG = 0;
Config cfg;

void cfgFabrica() {
  cfg.magia = MAGIA;
  cfg.seco1 = FAB_SECO;  cfg.molhado1 = FAB_MOLHADO;
  cfg.seco2 = FAB_SECO;  cfg.molhado2 = FAB_MOLHADO;
  cfg.reservVazioCm  = FAB_RESERV_VAZIO_CM;
  cfg.releAtivoBaixo = FAB_RELE_ATIVO_BAIXO;
  cfg.usarNivel      = FAB_USAR_NIVEL;
  cfg.buzzerPassivo  = FAB_BUZZER_PASSIVO;
  cfg.buzzerAtivoBaixo = FAB_BUZZER_ATIVO_BAIXO;
  cfg.usarTela       = FAB_USAR_TELA;
  cfg.telaVelIdx     = FAB_TELA_VEL_IDX;
}

Arduino_DataBus *bus = new Arduino_HWSPI(TFT_DC, TFT_CS);
Arduino_GFX *tela = new Arduino_ILI9488_18bit(bus, TFT_RST, 1 /* paisagem */, false);
bool telaOk = false;                // true so se begin() deu certo

const uint16_t PRETO = 0x0000, BRANCO = 0xFFFF, VERDE = 0x07E0, VERMELHO = 0xF800;
const uint16_t AZUL = 0x041F, AMARELO = 0xFFE0, CINZA = 0x8410;

int bruto1, bruto2, umid1, umid2, umidMedia;
int distCm = -1;
bool semEco = true;                 // -1 do HC-SR04: nao ligado ou fora de alcance
bool reservVazio = false;
bool bombaLigada = false;
bool modoAuto = true;
unsigned long bombaInicioMs = 0, ultimaRegaMs = 0, ultimaLeituraMs = 0, ultimaTelaMs = 0;
bool bipouVazio = false;
uint8_t vazioContagem = 0;
unsigned long ultimoAlertaMs = 0;

// ---------------- saidas ----------------
void rele(bool ligar) {
  digitalWrite(PINO_RELE, (ligar ^ cfg.releAtivoBaixo) ? HIGH : LOW);
  bombaLigada = ligar;
  digitalWrite(PINO_LED_AZ, ligar ? HIGH : LOW);
}

void releDesligadoCru() {           // sem mexer no estado, para o boot
  digitalWrite(PINO_RELE, cfg.releAtivoBaixo ? HIGH : LOW);
}

// Alguns modulos de buzzer apitam com nivel BAIXO. Mesmo truque do rele.
void buzzerNivel(bool apitar) {
  digitalWrite(PINO_BUZZER, (apitar ^ cfg.buzzerAtivoBaixo) ? HIGH : LOW);
}

void bip(int ms) {
  if (cfg.buzzerPassivo) { tone(PINO_BUZZER, 2500); delay(ms); noTone(PINO_BUZZER); }
  else { buzzerNivel(true); delay(ms); }
  buzzerNivel(false);
}

// ---------------- leitura ----------------
int paraPercentual(int bruto, int seco, int molhado) {
  if (seco == molhado) return 0;                  // evita divisao por zero
  long p = map(bruto, seco, molhado, 0, 100);
  return (int)constrain(p, 0, 100);
}

int lerAnalogicoMedio(uint8_t pino) {
  long soma = 0;
  for (int i = 0; i < 10; i++) { soma += analogRead(pino); delay(2); }
  return (int)(soma / 10);
}

int umPulsoCm() {
  digitalWrite(PINO_TRIG, LOW);  delayMicroseconds(3);
  digitalWrite(PINO_TRIG, HIGH); delayMicroseconds(10);
  digitalWrite(PINO_TRIG, LOW);
  unsigned long us = pulseIn(PINO_ECHO, HIGH, 25000UL);   // ~4 m
  if (us == 0) return -1;
  return (int)(us * 0.0343 / 2.0);
}

// mediana de 3 leituras validas: corta o pulso espurio que trancava a bomba
int lerDistanciaCm() {
  int v[3]; int n = 0;
  for (int i = 0; i < 3; i++) {
    int d = umPulsoCm();
    if (d > 0) v[n++] = d;
    delay(60);                      // HC-SR04 precisa de pausa entre disparos
  }
  if (n == 0) return -1;
  for (int i = 0; i < n - 1; i++)
    for (int j = i + 1; j < n; j++)
      if (v[j] < v[i]) { int t = v[i]; v[i] = v[j]; v[j] = t; }
  return v[n / 2];
}

void lerSensores() {
  bruto1 = lerAnalogicoMedio(PINO_UMID1);
  bruto2 = lerAnalogicoMedio(PINO_UMID2);
  umid1 = paraPercentual(bruto1, cfg.seco1, cfg.molhado1);
  umid2 = paraPercentual(bruto2, cfg.seco2, cfg.molhado2);
  umidMedia = (umid1 + umid2) / 2;
  distCm = cfg.usarNivel ? lerDistanciaCm() : umPulsoCm();
  semEco = (distCm < 0);
  // Com o ECHO solto a leitura oscila. Exige 3 leituras seguidas para mudar de
  // estado, senao o alerta de reservatorio fica piscando e bipando sem parar.
  bool vazioAgora = cfg.usarNivel && (semEco || distCm > cfg.reservVazioCm);
  if (vazioAgora == reservVazio) vazioContagem = 0;
  else if (++vazioContagem >= 3) { reservVazio = vazioAgora; vazioContagem = 0; }
}

// ---------------- controle ----------------
void controlarRega() {
  unsigned long agora = millis();
  if (bombaLigada) {
    bool tempoEsgotado = agora - bombaInicioMs >= BOMBA_MAX_MS;
    if (umidMedia >= DESLIGA_ACIMA_DE || tempoEsgotado || reservVazio || !modoAuto) {
      rele(false);
      ultimaRegaMs = agora;
    }
    return;
  }
  if (!modoAuto || reservVazio) return;
  bool podeRegar = ultimaRegaMs == 0 || agora - ultimaRegaMs >= ESPERA_MIN_MS;
  if (umidMedia < LIGA_ABAIXO_DE && podeRegar) {
    rele(true);
    bombaInicioMs = agora;
  }
}

void atualizarLeds() {
  bool alerta = reservVazio || umidMedia < LIGA_ABAIXO_DE;
  digitalWrite(PINO_LED_VM, alerta ? HIGH : LOW);
  digitalWrite(PINO_LED_VD, alerta ? LOW : HIGH);
  // Alerta de reservatorio vazio: 2 bipes ao entrar no estado, e no maximo uma
  // vez a cada 30 s. Numa feira, buzzer repetindo sem parar e insuportavel.
  unsigned long agora = millis();
  bool podeAlertar = (ultimoAlertaMs == 0) || (agora - ultimoAlertaMs >= 30000UL);
  if (reservVazio && !bipouVazio && podeAlertar) {
    bip(150); delay(100); bip(150);
    bipouVazio = true;
    ultimoAlertaMs = agora;
  }
  if (!reservVazio) bipouVazio = false;
}

// ---------------- serial ----------------
void imprimirSerial() {
  Serial.print(F("U1=")); Serial.print(umid1); Serial.print(F("% (")); Serial.print(bruto1);
  Serial.print(F(")  U2=")); Serial.print(umid2); Serial.print(F("% (")); Serial.print(bruto2);
  Serial.print(F(")  media=")); Serial.print(umidMedia);
  Serial.print(F("%  dist="));
  if (semEco) Serial.print(F("SEM ECO")); else { Serial.print(distCm); Serial.print(F("cm")); }
  Serial.print(F("  reserv="));
  if (!cfg.usarNivel)   Serial.print(F("(nao usa)"));
  else if (reservVazio) Serial.print(semEco ? F("VAZIO? sem eco") : F("VAZIO"));
  else                  Serial.print(F("ok"));
  Serial.print(F("  bomba=")); Serial.print(bombaLigada ? F("LIGADA") : F("desl"));
  Serial.print(F("  auto=")); Serial.println(modoAuto ? F("sim") : F("nao"));
}

void mostrarConfig() {
  Serial.println(F("--- configuracao ---"));
  Serial.print(F("S1 seco/molhado: ")); Serial.print(cfg.seco1); Serial.print('/'); Serial.println(cfg.molhado1);
  Serial.print(F("S2 seco/molhado: ")); Serial.print(cfg.seco2); Serial.print('/'); Serial.println(cfg.molhado2);
  Serial.print(F("usar nivel: ")); Serial.println(cfg.usarNivel ? F("sim") : F("nao"));
  Serial.print(F("reservatorio vazio acima de: ")); Serial.print(cfg.reservVazioCm); Serial.println(F(" cm"));
  Serial.print(F("rele ativo em: ")); Serial.println(cfg.releAtivoBaixo ? F("BAIXO") : F("ALTO"));
  Serial.print(F("buzzer: ")); Serial.print(cfg.buzzerPassivo ? F("passivo (tone)") : F("ativo"));
  Serial.print(F(", apita em ")); Serial.println(cfg.buzzerAtivoBaixo ? F("BAIXO") : F("ALTO"));
  Serial.print(F("SPI da tela: "));
  Serial.print(TELA_VEL[cfg.telaVelIdx < TELA_VEL_N ? cfg.telaVelIdx : FAB_TELA_VEL_IDX] / 1000000L);
  Serial.println(F(" MHz"));
  Serial.print(F("tela: ")); Serial.print(cfg.usarTela ? F("ligada") : F("desligada"));
  // ATENCAO: o ILI9488 nao responde pelo SPI, entao begin() da certo mesmo sem a tela
  // ligada. "begin ok" significa so que o barramento subiu - confirme olhando a tela.
  Serial.println(telaOk ? F(" (begin ok - olhe a tela p/ confirmar)") : F(" (begin falhou)"));
  Serial.print(F("rega: abaixo de ")); Serial.print(LIGA_ABAIXO_DE);
  Serial.print(F("% ate ")); Serial.print(DESLIGA_ACIMA_DE); Serial.println(F("%"));
  Serial.println(F("--------------------"));
}

void ajuda() {
  Serial.println(F("r=leituras p=config b=bomba3s a=auto l=LEDs z=buzzer"));
  Serial.println(F("1=sensores no AR (seco)  2=sensores na AGUA (molhado)"));
  Serial.println(F("n=usar nivel  v=vazio=dist atual  i=inverte rele"));
  Serial.println(F("y=buzzer ativo/passivo  u=buzzer apita em ALTO/BAIXO"));
  Serial.println(F("d=tela on/off  t=redesenha  f=velocidade do SPI da tela"));
  Serial.println(F("g=GRAVA na EEPROM  x=fabrica  ?=ajuda"));
}

// ---------------- tela ----------------
void telaFixa() {
  if (!telaOk) return;
  tela->fillScreen(PRETO);
  tela->fillRect(0, 0, 480, 44, AZUL);
  tela->setTextColor(BRANCO, AZUL);
  tela->setTextSize(3);
  tela->setCursor(12, 10);
  tela->print(F("ROBO IRRIGADOR"));
  tela->setTextSize(2);
  tela->setTextColor(CINZA, PRETO);
  tela->setCursor(12, 60);  tela->print(F("Umidade sensor 1"));
  tela->setCursor(12, 110); tela->print(F("Umidade sensor 2"));
  tela->setCursor(12, 170); tela->print(F("Reservatorio"));
  tela->setCursor(12, 230); tela->print(F("Bomba"));
}

void barra(int y, int pct, uint16_t cor) {
  const int x = 250, w = 210, h = 22;
  tela->drawRect(x, y, w, h, BRANCO);
  int cheio = (w - 4) * pct / 100;
  tela->fillRect(x + 2, y + 2, cheio, h - 4, cor);
  tela->fillRect(x + 2 + cheio, y + 2, (w - 4) - cheio, h - 4, PRETO);
}

void campo(int x, int y, uint16_t cor, const char *txt) {
  char buf[16];
  snprintf(buf, sizeof(buf), "%-12s", txt);         // apaga texto anterior
  tela->setTextSize(3);
  tela->setTextColor(cor, PRETO);
  tela->setCursor(x, y);
  tela->print(buf);
}

void telaDados() {
  if (!telaOk) return;
  char t[16];
  uint16_t c1 = umid1 < LIGA_ABAIXO_DE ? VERMELHO : VERDE;
  uint16_t c2 = umid2 < LIGA_ABAIXO_DE ? VERMELHO : VERDE;
  snprintf(t, sizeof(t), "%3d%%", umid1); campo(12, 80, c1, t);  barra(80, umid1, c1);
  snprintf(t, sizeof(t), "%3d%%", umid2); campo(12, 130, c2, t); barra(130, umid2, c2);
  if (!cfg.usarNivel)   { if (semEco) snprintf(t, sizeof(t), "--"); else snprintf(t, sizeof(t), "%d cm", distCm); }
  else if (reservVazio)   snprintf(t, sizeof(t), "VAZIO");
  else                    snprintf(t, sizeof(t), "OK %dcm", distCm);
  campo(12, 190, reservVazio ? VERMELHO : VERDE, t);
  campo(12, 250, bombaLigada ? AZUL : BRANCO, bombaLigada ? "REGANDO" : (modoAuto ? "AUTO" : "MANUAL"));
  tela->setTextSize(2);
  tela->setTextColor(AMARELO, PRETO);
  tela->setCursor(12, 295);
  tela->print(F("Rega abaixo de ")); tela->print(LIGA_ABAIXO_DE); tela->print(F("%  "));
}

void iniciarTela() {
  telaOk = false;
  if (!cfg.usarTela) { Serial.println(F("tela desligada na config")); return; }
  if (cfg.telaVelIdx >= TELA_VEL_N) cfg.telaVelIdx = FAB_TELA_VEL_IDX;
  int32_t vel = TELA_VEL[cfg.telaVelIdx];
  Serial.print(F("tela: SPI a ")); Serial.print(vel / 1000000L); Serial.println(F(" MHz"));
  if (!tela->begin(vel)) {
    Serial.println(F("ERRO: tela nao iniciou (ver nivel 3,3V / ligacao). Segue sem tela."));
    return;
  }
  telaOk = true;
  telaFixa();
}

// ---------------- comandos ----------------
void comandos() {
  if (!Serial.available()) return;
  char c = Serial.read();
  switch (c) {
    case 'r': lerSensores(); imprimirSerial(); break;
    case 'p': mostrarConfig(); break;

    case 'b':
      Serial.println(F("Teste da bomba: 3 s"));
      rele(true); delay(3000); rele(false); ultimaRegaMs = millis(); break;

    case 'a': modoAuto = !modoAuto; if (!modoAuto) rele(false);
      Serial.print(F("Modo automatico: ")); Serial.println(modoAuto ? F("LIGADO") : F("DESLIGADO")); break;

    case 'l': {
      const uint8_t leds[] = {PINO_LED_VD, PINO_LED_VM, PINO_LED_AZ};
      for (uint8_t p : leds) { digitalWrite(p, HIGH); delay(400); digitalWrite(p, LOW); }
      break;
    }
    case 'z': bip(300); break;

    case '1':
      bruto1 = lerAnalogicoMedio(PINO_UMID1); bruto2 = lerAnalogicoMedio(PINO_UMID2);
      cfg.seco1 = bruto1; cfg.seco2 = bruto2;
      Serial.print(F("SECO gravado: S1=")); Serial.print(cfg.seco1);
      Serial.print(F(" S2=")); Serial.println(cfg.seco2);
      Serial.println(F("(use 'g' para manter apos desligar)")); break;

    case '2':
      bruto1 = lerAnalogicoMedio(PINO_UMID1); bruto2 = lerAnalogicoMedio(PINO_UMID2);
      cfg.molhado1 = bruto1; cfg.molhado2 = bruto2;
      Serial.print(F("MOLHADO gravado: S1=")); Serial.print(cfg.molhado1);
      Serial.print(F(" S2=")); Serial.println(cfg.molhado2);
      if (cfg.seco1 <= cfg.molhado1 || cfg.seco2 <= cfg.molhado2)
        Serial.println(F("AVISO: seco deve ser MAIOR que molhado. Refazer 1 e 2."));
      Serial.println(F("(use 'g' para manter apos desligar)")); break;

    case 'n': cfg.usarNivel = !cfg.usarNivel;
      Serial.print(F("Usar nivel do reservatorio: ")); Serial.println(cfg.usarNivel ? F("SIM") : F("NAO")); break;

    case 'v': {
      int d = lerDistanciaCm();
      if (d < 0) { Serial.println(F("sem eco: nao deu para medir")); break; }
      cfg.reservVazioCm = d;
      Serial.print(F("Reservatorio VAZIO acima de ")); Serial.print(d); Serial.println(F(" cm"));
      Serial.println(F("(use 'g' para manter apos desligar)")); break;
    }

    case 'i': cfg.releAtivoBaixo = !cfg.releAtivoBaixo; rele(false);
      Serial.print(F("Rele ativo em: ")); Serial.println(cfg.releAtivoBaixo ? F("BAIXO") : F("ALTO")); break;

    case 'y': cfg.buzzerPassivo = !cfg.buzzerPassivo; buzzerNivel(false);
      Serial.print(F("Buzzer: ")); Serial.println(cfg.buzzerPassivo ? F("PASSIVO (tone)") : F("ATIVO"));
      bip(200); break;

    case 'u': cfg.buzzerAtivoBaixo = !cfg.buzzerAtivoBaixo; buzzerNivel(false);
      Serial.print(F("Buzzer apita em: ")); Serial.println(cfg.buzzerAtivoBaixo ? F("BAIXO") : F("ALTO"));
      Serial.println(F("(se calou agora, era isso. use 'g' para gravar)"));
      break;

    case 'd': cfg.usarTela = !cfg.usarTela;
      Serial.print(F("Tela: ")); Serial.println(cfg.usarTela ? F("LIGADA") : F("DESLIGADA"));
      if (cfg.usarTela) iniciarTela(); else telaOk = false; break;

    case 't': iniciarTela(); telaDados(); break;

    case 'f': cfg.telaVelIdx = (cfg.telaVelIdx + 1) % TELA_VEL_N;
      Serial.print(F("SPI da tela -> ")); Serial.print(TELA_VEL[cfg.telaVelIdx] / 1000000L);
      Serial.println(F(" MHz"));
      Serial.println(F("(divisor com resistor alto pede 1 ou 2 MHz)"));
      iniciarTela(); telaDados(); break;

    case 'g': EEPROM.put(ENDERECO_CFG, cfg);
      Serial.println(F("Configuracao GRAVADA na EEPROM.")); bip(80); break;

    case 'x': cfgFabrica(); rele(false);
      Serial.println(F("Valores de fabrica carregados (use 'g' para gravar).")); break;

    case '?': ajuda(); break;
  }
}

// ---------------- setup / loop ----------------
void setup() {
  EEPROM.get(ENDERECO_CFG, cfg);
  bool daEeprom = (cfg.magia == MAGIA);
  if (!daEeprom) cfgFabrica();

  // rele desligado ANTES de virar saida, para a bomba nao dar um pulso no boot
  releDesligadoCru();
  pinMode(PINO_RELE, OUTPUT);
  pinMode(PINO_TRIG, OUTPUT);
  pinMode(PINO_ECHO, INPUT);
  pinMode(PINO_LED_VD, OUTPUT);
  pinMode(PINO_LED_VM, OUTPUT);
  pinMode(PINO_LED_AZ, OUTPUT);
  // buzzer em repouso ANTES de virar saida, para nao apitar no boot
  digitalWrite(PINO_BUZZER, cfg.buzzerAtivoBaixo ? HIGH : LOW);
  pinMode(PINO_BUZZER, OUTPUT);
  buzzerNivel(false);
  rele(false);

  Serial.begin(115200);
  Serial.println(F("Robo irrigador - FAZ Makerspace - OS-2026-0885"));
  Serial.println(daEeprom ? F("configuracao: da EEPROM") : F("configuracao: de fabrica"));

  iniciarTela();        // ANTES do mostrarConfig(), senao o boot sempre diz "nao iniciou"
  mostrarConfig();
  ajuda();
  bip(80);
}

void loop() {
  comandos();
  unsigned long agora = millis();
  if (agora - ultimaLeituraMs >= 500) {
    ultimaLeituraMs = agora;
    lerSensores();
    controlarRega();
    atualizarLeds();
  }
  if (agora - ultimaTelaMs >= 1000) {
    ultimaTelaMs = agora;
    imprimirSerial();
    telaDados();
  }
}
