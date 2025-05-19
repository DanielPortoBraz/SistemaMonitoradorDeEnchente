# Sistema Monitorador de Enchente

Este projeto simula um sistema de monitoramento de enchentes baseado em um microcontrolador **Raspberry Pi Pico**, utilizando o **FreeRTOS** para gerenciar tarefas concorrentes. O sistema foi desenvolvido sobre a base de simulação da **BitDogLab**, permitindo visualização em tempo real de dados simulados de sensores e atuadores.

## Objetivo

Monitorar o nível de água e o volume de chuva com sensores simulados (joystick analógico) e reagir a níveis críticos através de uma série de periféricos visuais e sonoros, com o objetivo de alertar situações de possível enchente.

---

## Periféricos Utilizados

### Display OLED SSD1306
- Protocolo I2C
- Mostra os valores de nível de água e volume de chuva em barras e porcentagens.
- Exibe o aviso de **"ALERTA"** em caso de níveis críticos.

### Joystick Analógico (Simulação de sensores)
- Eixos X (nível de água) e Y (volume de chuva) conectados aos canais ADC 0 e 1.
- Leitura a 10 Hz via `vJoystickTask`.

### LED RGB
- Verde: funcionamento normal.
- Vermelho piscando: modo de alerta.

### Matriz de LEDs WS2812b (25 LEDs)
- Mostra um padrão de alerta visual em caso de enchente.
- Controlada via PIO e DMA.

### Buzzer
- Emite tons alternados como alerta sonoro.
- Controlado via PWM.

---

## Organização do Código

O sistema é dividido em tarefas FreeRTOS:

| Tarefa | Função |
|-------|--------|
| `vJoystickTask` | Captura os valores dos sensores via ADC |
| `vDisplayTask` | Atualiza o display OLED com os dados e alerta |
| `vLedRgbTask` | Alterna o LED RGB entre verde e vermelho |
| `vMatrixTask` | Exibe alerta visual na matriz WS2812b |
| `vBuzzerTask` | Emite sinal sonoro com PWM no modo de alerta |

---

## Estrutura do Projeto

```
SistemaMonitoradorDeEnchente/
│
├── lib/
│   ├── ssd1306.h        # Biblioteca do display OLED
│   ├── font.h           # Fontes do display
│
├── ws2818b.pio.h        # Programa da PIO para WS2812b
├── img_disp.h           # Imagem padrão do display
├── figures.h            # Padrão de alerta da matriz LED
├── main.c               # Código principal do projeto
├── CMakeLists.txt       # Configuração de build com CMake
└── README.md            # Este arquivo
```

---

## Simulação com BitDogLab

Este projeto pode ser testado no simulador **BitDogLab**, que permite a simulação gráfica dos periféricos:

- Simula o display OLED SSD1306.
- Visualiza o estado do LED RGB e buzzer.
- Mostra a matriz WS2812b em tempo real.
- Permite controlar o joystick via sliders.

**Importante**: A simulação no BitDogLab requer configuração dos pinos conforme especificado no código.

---

## Compilação e Execução

### Pré-requisitos
- SDK do Raspberry Pi Pico
- FreeRTOS portado para RP2040
- CMake
- BitDogLab (para simulação)

### Passos
```bash
mkdir build && cd build
cmake ..
make
```

O binário pode ser carregado no simulador ou no dispositivo físico.

---

## Licença

Este projeto está licenciado sob a [MIT License](LICENSE).
