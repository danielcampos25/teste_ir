# teste_ir

Projeto provisório para testar a recepção de sinais infravermelhos usando
ESP-IDF e o periférico RMT.

## Estado atual

- Framework: ESP-IDF 6.1.0
- Alvo configurado: ESP32
- GPIO de entrada IR: `GPIO4`
- Resolução do RMT: `1 MHz`
- Diretório de saída: `build/` (gerado localmente e não versionado)

O firmware captura os símbolos brutos do sinal infravermelho e os imprime no
log serial. A decodificação de protocolos (por exemplo, NEC ou RC5) ainda não
faz parte deste protótipo.

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

## Próximos passos

- Validar a captura com o hardware conectado.
- Identificar o protocolo do controle remoto.
- Adicionar decodificação e testes dos comandos recebidos.
