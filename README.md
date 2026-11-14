# Robô Irrigador

Irrigador automático para um robô de feira de ciências, feito com **Arduino Mega 2560**.

O robô mede a umidade da terra, compara com uma regra e decide sozinho se rega. Não existe
horário de rega: ele rega **por necessidade**. A ideia por trás disso é simples — regar demais
mata a planta tanto quanto regar de menos, porque a raiz precisa de ar e terra encharcada não
tem ar nenhum entre os grãos.

Tudo o que só se descobre na bancada (calibração dos sensores, lógica do relé, tipo do buzzer,
velocidade do SPI da tela) se ajusta **por comando na Serial** e fica gravado na EEPROM. Não é
preciso regravar o programa para calibrar.

---

## Hardware

| Peça | Observação |
|---|---|
| Arduino Mega 2560 | o Mega é necessário: a tela e os sensores juntos pedem mais pinos e mais RAM do que o Uno tem |
| Display TFT 3.5" ILI9488 (SPI) | ligado em 3,3 V — os sinais vêm do Mega por divisor resistivo |
| 2 × sensor capacitivo de umidade de solo | capacitivo, não o resistivo: o resistivo corrói em poucas semanas |
| HC-SR04 | mede o nível de água do reservatório |
| Módulo relé 5 V, 1 canal | liga a bomba; contatos isolados da bobina |
| Mini bomba submersa 3–6 V | com fonte própria |
| LEDs verde, vermelho e azul | estado do sistema |
| Buzzer | avisos sonoros |
| Diodo 1N4007 | em paralelo com a bomba, protege o contato do relé |

A bomba tem **fonte própria**, separada da do Arduino: a corrente de partida dela derrubaria o
Mega, e o ruído elétrico do motorzinho estragaria justamente a leitura de umidade. Os contatos
do relé são secos e isolados da bobina, então os dois circuitos não precisam de terra em comum.

## Ligações

| Função | Pino do Mega |
|---|---|
| Sensor de umidade 1 | A0 |
| Sensor de umidade 2 | A1 |
| HC-SR04 TRIG | 22 |
| HC-SR04 ECHO | 23 |
| Entrada do relé | 24 |
| LED verde (solo ok) | 26 |
| LED vermelho (solo seco / reservatório vazio) | 27 |
| LED azul (regando) | 28 |
| Buzzer | 30 |
| TFT RST | 48 |
| TFT DC | 49 |
| TFT MOSI | 51 |
| TFT SCK | 52 |
| TFT CS | 53 |

## Biblioteca

Uma só: **GFX Library for Arduino** (moononournation), que fala com o ILI9488 por SPI.

```
arduino-cli lib install "GFX Library for Arduino"
```

## Compilar e gravar

```
arduino-cli compile --fqbn arduino:avr:mega robo_irrigador
arduino-cli upload  --fqbn arduino:avr:mega -p COM20 robo_irrigador
```

Troque `COM20` pela porta da sua placa. Pela IDE do Arduino funciona igual: basta abrir
`robo_irrigador/robo_irrigador.ino` e escolher a placa **Arduino Mega or Mega 2560**.

> Gravar o programa apaga a configuração da EEPROM e volta tudo para os valores de fábrica.
> Depois de gravar, refaça a calibração e grave de novo com `g`.

## Regras de rega

| Regra | Valor |
|---|---|
| Liga a bomba abaixo de | 35 % de umidade média |
| Desliga acima de | 60 % de umidade média |
| Tempo máximo de bomba por vez | 8 s |
| Espera mínima entre regas | 15 s (a água precisa infiltrar antes da próxima medida) |

Essas quatro linhas ficam no topo do sketch e são fáceis de mudar.

## Travas de segurança

- **Sem água, não liga.** É a própria água que mantém a bombinha fria por dentro; girando a
  seco ela esquenta depressa e queima. Entre não regar e regar sem água, o robô prefere não regar.
- **Nunca mais de 8 segundos** de bomba por vez, mesmo que algo dê errado na leitura.
- **15 segundos de espera** entre uma rega e outra, para a terra não ser medida molhada por fora
  e seca por dentro.
- O relé é ligado em **COM + NO**: se faltar energia no controle, o contato abre e a bomba para.

## Comandos pela Serial

Monitor Serial a **115200 baud**, fim de linha **"Nova linha"**.

| Comando | O que faz |
|---|---|
| `r` | mostra as leituras agora |
| `p` | mostra a configuração |
| `b` | testa a bomba por 3 s |
| `a` | liga/desliga o modo automático |
| `l` | testa os LEDs |
| `z` | testa o buzzer |
| `1` | sensores **no ar** → grava como SECO |
| `2` | sensores **na água** → grava como MOLHADO |
| `n` | usa ou ignora o sensor de nível |
| `v` | usa a distância atual como "reservatório vazio" |
| `i` | inverte a lógica do relé |
| `y` | alterna buzzer ativo / passivo |
| `d` | liga/desliga a tela |
| `t` | redesenha a tela |
| `g` | **grava** a configuração na EEPROM |
| `x` | volta aos valores de fábrica (não grava; use `g` depois) |
| `?` | ajuda |

## Calibração em 4 passos

1. Com os sensores **secos, no ar**, mande `1`.
2. Mergulhe os sensores **na água** até a marca e mande `2`.
3. Com o reservatório no nível que você considera vazio, mande `v`.
4. Mande `g` para gravar. Desligue e ligue a placa: `p` deve dizer `configuracao: da EEPROM`.

O número do sensor capacitivo **cai** quando molha (nesta bancada: ~735 no ar seco, ~425 na
água). Isso é normal e o programa já conta com isso.

## Licença

MIT — veja [LICENSE](LICENSE).
