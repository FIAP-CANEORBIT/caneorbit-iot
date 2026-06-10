# CaneOrbit — IoT ESP32

> Monitoramento agrícola de precisão para cultivo de cana-de-açúcar com ESP32, sensores de campo e integração com a API CaneOrbit.

**Disciplina:** Disruptive Architectures — FIAP  
**Global Solution 2026/1 — Space Connect**

---

## Integrantes

| Nome | RM |
| :--- | :--- |
| Diego Andrade | RM566385 |
| Grazielle De Alencar | RM561529 |
| Julia Corrêa | RM564870 |

| **Video Pitch Projeto (3 min)** | https://youtu.be/izUM9V-_D7g |

---

## Descrição

O **CaneOrbit** é um dispositivo IoT baseado no ESP32 que monitora em tempo real as condições do solo e do ambiente em lavouras de cana-de-açúcar. Os dados coletados pelos sensores são enviados automaticamente para a API CaneOrbit em nuvem, permitindo histórico, análise e tomada de decisão baseada em dados.

O dispositivo também serve um dashboard web local acessível por qualquer navegador na mesma rede, exibindo leituras em tempo real e o nível de risco calculado diretamente no microcontrolador.

---

## Arquitetura da solução

```
┌─────────────────────────────────────┐
│           ESP32 DevKit v1           │
│                                     │
│  DHT22 ──── Temp + Umidade do ar    │
│  Joystick ─ Umidade do solo         │
│  Potênc. ── pH do solo              │
│                                     │
│  LCD 16x2 ─ Exibição local          │
│  LED verde─ API conectada           │
│  LED verm. ─ Sem conexão            │
│                                     │
│  WebServer porta 80 (dashboard)     │
└──────────────┬──────────────────────┘
               │ Wi-Fi / HTTP
               ▼
┌─────────────────────────────────────┐
│   CaneOrbit API Java (Render)       │
│   POST /api/leituras                │
│   https://caneorbis-api-java        │
│        .onrender.com                │
└─────────────────────────────────────┘
```

---

## Componentes e pinagem

| Componente | Tipo | Pino ESP32 |
| :--- | :--- | :--- |
| DHT22 | Temperatura + umidade do ar | GPIO 4 |
| Joystick analógico (VRX) | Umidade do solo (0–100%) | GPIO 34 |
| Potenciômetro | pH do solo (0–14) | GPIO 35 |
| LCD 1602 I2C — SDA | Display | GPIO 21 |
| LCD 1602 I2C — SCL | Display | GPIO 22 |
| LED verde (220Ω) | API conectada / POST ok | GPIO 25 |
| LED vermelho (220Ω) | Sem conexão / erro | GPIO 26 |

> Em simulação Wokwi, o joystick e o potenciômetro substituem sensores físicos. Em hardware real, substituir o joystick por um sensor capacitivo (YL-69 ou similar) e o potenciômetro por um módulo sensor de pH analógico.

---

## Funcionalidades

- Leitura a cada 2 segundos de umidade do solo, temperatura, umidade do ar e pH
- Cálculo de risco da lavoura diretamente no ESP32:
  - **Baixo** — solo ≥ 40%, temp ≤ 33°C, pH entre 6,0 e 7,5
  - **Atenção** — solo entre 20–40%, temp entre 33–38°C, ou pH entre 5,5–6,0 / 7,5–8,5
  - **Alto** — solo < 20%, temp > 38°C, ou pH < 5,5 / > 8,5
- Envio automático a cada 10 segundos para `POST /api/leituras` da API CaneOrbit
- LED verde acende ao confirmar POST com sucesso (HTTP 200/201)
- LED vermelho pisca enquanto conecta ao Wi-Fi; fica fixo em caso de erro na API
- Display LCD exibe umidade do solo, temperatura, pH e status de risco
- Dashboard HTML local (porta 80) com atualização automática a cada 5 segundos
- Três endpoints REST próprios do ESP32:
  - `GET /sensor/leitura` — leitura atual em JSON
  - `GET /sensor/status` — status da API e nível de risco
  - `POST /sensor/config` — recebe configurações externas

---

## Integração com a API CaneOrbit

A API Java está em produção no Render:

**Base URL:** `https://caneorbis-api-java.onrender.com`

### Endpoint consumido pelo ESP32

**`POST /api/leituras`** — sem autenticação

```json
{
  "idDispositivo": 1,
  "umidadeSolo": 45.5,
  "temperatura": 28.3,
  "phSolo": 6.2
}
```

Resposta esperada: `201 Created`

### Pré-requisito: cadastrar o dispositivo

Antes de rodar o firmware, é necessário registrar o dispositivo na API pelo Swagger (`/swagger-ui/index.html`) e obter o `id` retornado. Esse ID deve ser configurado no firmware:

```cpp
const int DEVICE_ID = 1; // substitua pelo id retornado
```

Fluxo de cadastro:
1. `POST /api/usuarios/register` — criar conta
2. `POST /api/auth/login` — obter token JWT
3. `POST /api/propriedades` — criar propriedade (requer token)
4. `POST /api/dispositivos` — criar dispositivo (requer token) → **copiar o `id` retornado**

---

## Estrutura do projeto

```
harvesteye-iot/
├── src/
│   └── main.cpp          # Firmware principal
├── diagram.json          # Circuito Wokwi
├── wokwi.toml            # Configuração do simulador
├── platformio.ini        # Configuração PlatformIO
└── README.md
```

---

## Como executar

### Simulação (Wokwi + PlatformIO)

**Pré-requisitos:**
- VS Code
- Extensão PlatformIO IDE
- Extensão Wokwi for VS Code (Community License)

**Passos:**

1. Clone o repositório:
```bash
git clone https://github.com/SEU_USUARIO/harvesteye-iot.git
cd harvesteye-iot
```

2. Abra no VS Code e aguarde o PlatformIO instalar as dependências.

3. Configure o `DEVICE_ID` no `src/main.cpp` com o ID do dispositivo cadastrado na API.

4. Compile o firmware:
```
Ctrl + Alt + B
```

5. Inicie a simulação:
```
Ctrl + Shift + P → Wokwi: Start Simulator
```

6. Interaja com os sensores:
   - **Joystick** — simula a umidade do solo
   - **Potenciômetro** — simula o pH do solo
   - **DHT22** — ajuste temperatura pelo slider lateral

### Hardware físico

1. Monte o circuito conforme a pinagem descrita acima
2. Altere o `SSID` e `PASSWORD` no `main.cpp` para a rede local
3. Compile e grave via PlatformIO (`Upload`)
4. Acesse o dashboard pelo IP exibido no LCD após a conexão Wi-Fi

---

## Dependências (platformio.ini)

```ini
[env:harvsteye]
platform    = espressif32
board       = esp32dev
framework   = arduino

lib_deps =
    adafruit/DHT sensor library
    adafruit/Adafruit Unified Sensor
    marcoschwartz/LiquidCrystal_I2C
    bblanchon/ArduinoJson
```

---

## LCD — formato de exibição

```
Linha 1: S:39% T:24C
Linha 2: pH:6.8 [OK]
```

Status possíveis: `[OK]` risco baixo · `[AT]` atenção · `[RI]` risco alto · `[--]` sem dados

---

## Links

| Recurso | URL |
| :--- | :--- |
| API Java (produção) | https://caneorbis-api-java.onrender.com |
| Swagger Java | https://caneorbis-api-java.onrender.com/swagger-ui/index.html |
| Repositório backend | https://github.com/FIAP-CANEORBIT/caneorbis-api-dotnet |