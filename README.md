# teste_ir

Projeto provisório para testar a recepção de sinais infravermelhos usando
ESP-IDF e o periférico RMT.

## Estado atual

- Framework: ESP-IDF 6.1.0
- Alvo configurado: ESP32
- GPIO de entrada IR: `GPIO4`
- Resolução do RMT: `1 MHz`
- Diretório de saída: `build/` (gerado localmente e não versionado)

O receptor decodifica o quadro de 13 bytes usado pelo controle Elgin
(cabeçalho de 9 ms/4,5 ms, bits MSB-first) e imprime os bytes recebidos e as
alterações em relação ao quadro anterior. Isso permite identificar o byte que
representa temperatura, LIGAR/DESLIGAR e os comandos de aumentar/diminuir.

## Pré-requisitos

- ESP-IDF 6.1.0 configurado no ambiente
- Um dispositivo ESP32
- Receptor infravermelho conectado ao `GPIO4` e ao GND/3V3 conforme o módulo

## Compilar e gravar

No terminal com o ambiente do ESP-IDF carregado:

```bash
idf.py set-target esp32
idf.py build
idf.py -p PORT flash monitor
```

Substitua `PORT` pela porta serial do dispositivo, por exemplo
`/dev/ttyUSB0`.

Para sair do monitor serial, use `Ctrl-]`.

## Estrutura

```text
.
├── CMakeLists.txt
├── main/
│   ├── CMakeLists.txt
│   └── teste_ir.c
└── sdkconfig.defaults
```

O arquivo `sdkconfig` é gerado pelo ESP-IDF e não deve ser versionado. Use
`sdkconfig.defaults` para registrar opções mínimas e reproduzíveis do projeto.

## Identificação dos comandos

Grave no monitor serial um quadro para cada ação do controle: LIGAR em cada
temperatura, DESLIGAR, aumentar e diminuir. O receptor exibirá, por exemplo:

```text
Quadro: C3 EE 07 00 04 00 02 00 00 04 00 A0 F9
Alteracao: byte[9] 0x00 -> 0x04
```

O byte indicado nas linhas `Alteracao` é a evidência necessária para concluir
a codificação da temperatura. Os quadros LIGAR/DESLIGAR existentes, sozinhos,
não permitem distinguir se `byte[9]` é temperatura, estado ou um campo
combinado; ainda é necessário capturar os botões de temperatura.

## Protocolo armazenado no transmissor

O transmissor calcula os códigos de temperatura de 16 °C a 32 °C em
`main/transmissor_ir.c`. O padrão observado é a inversão dos bits de uma
sequência que avança 8 unidades por grau:

| Temperatura | `byte[1]` |
|---:|---:|
| 16 | `0xE2` |
| 17 | `0xF2` |
| 18 | `0xEA` |
| 19 | `0xFA` |
| 20 | `0xE6` |
| 21 | `0xF6` |
| 22 | `0xEE` |
| 23 | `0xFE` |
| 24 | `0xE1` |
| 25 | `0xF1` |
| 26 | `0xE9` |
| 27 | `0xF9` |
| 28 | `0xE5` |
| 29 | `0xF5` |
| 30 | `0xED` |
| 31 | `0xFD` |
| 32 | `0xE3` |

Os comandos usam `byte[11] = 0xA0` para ligar/desligar, `0x00` para
aumentar e `0x80` para diminuir. O transmissor calcula o último byte a partir
da temperatura, do comando e do estado de energia.

A versão atual do transmissor executa automaticamente uma sequência de teste:
liga em 21 °C, aumenta para 22 °C, diminui para 21 °C e desliga, com
intervalos de 10 segundos entre os comandos. As funções `ligar`,
`desligar`, `aumentar_temperatura` e `diminuir_temperatura` ficam no código
para serem reutilizadas posteriormente por outro controle de entrada.

A temperatura inicia em 21 °C e fica limitada ao intervalo de 16 °C a 32 °C.
